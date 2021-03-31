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

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/local_storage/NoteSearchQuery.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Compat.h>
#include <quentier/utility/SuppressWarnings.h>
#include <quentier/utility/SysInfo.h>

#include <QMetaMethod>

namespace quentier {

class LocalStorageManagerAsyncPrivate
{
public:
    ~LocalStorageManagerAsyncPrivate()
    {
        delete m_pLocalStorageCacheManager;
        delete m_pLocalStorageManager;
    }

    void setUseCache(const bool useCache)
    {
        if (m_useCache) {
            // Cache is being disabled - no point to store things in it anymore,
            // it would get rotten pretty quick
            m_pLocalStorageCacheManager->clear();
        }

        m_useCache = useCache;
    }

    void cacheNotes(
        const QList<Note> & notes,
        const LocalStorageManager::GetNoteOptions options)
    {
        if (!m_useCache) {
            return;
        }

        if (!(options &
              LocalStorageManager::GetNoteOption::WithResourceMetadata)) {
            return;
        }

        if (options &
            LocalStorageManager::GetNoteOption::WithResourceBinaryData) {
            for (auto note: notes) {
                note.setResources(QList<Resource>());
                m_pLocalStorageCacheManager->cacheNote(note);
            }
        }
        else {
            for (const auto & note: qAsConst(notes)) {
                m_pLocalStorageCacheManager->cacheNote(note);
            }
        }
    }

    Account m_account;
    bool m_useCache = true;

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    LocalStorageManager::StartupOptions m_startupOptions;
#else
    LocalStorageManager::StartupOptions m_startupOptions = 0;
#endif

    LocalStorageManager * m_pLocalStorageManager = nullptr;
    LocalStorageCacheManager * m_pLocalStorageCacheManager = nullptr;
};

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
    for (auto & resource: noteResources) {
        resource.setDataBody(QByteArray());
        resource.setAlternateDataBody(QByteArray());
    }
    note.setResources(noteResources);
}

} // namespace

LocalStorageManagerAsync::LocalStorageManagerAsync(
    const Account & account, const LocalStorageManager::StartupOptions options,
    QObject * parent) :
    QObject(parent),
    d_ptr(new LocalStorageManagerAsyncPrivate)
{
    Q_D(LocalStorageManagerAsync);
    d->m_account = account;
    d->m_startupOptions = options;
}

LocalStorageManagerAsync::~LocalStorageManagerAsync()
{
    delete d_ptr;
}

void LocalStorageManagerAsync::setUseCache(const bool useCache)
{
    Q_D(LocalStorageManagerAsync);
    d->setUseCache(useCache);
}

const LocalStorageCacheManager *
LocalStorageManagerAsync::localStorageCacheManager() const
{
    Q_D(const LocalStorageManagerAsync);

    if (!d->m_useCache) {
        return nullptr;
    }
    else {
        return d->m_pLocalStorageCacheManager;
    }
}

bool LocalStorageManagerAsync::installCacheExpiryFunction(
    const ILocalStorageCacheExpiryChecker & checker)
{
    Q_D(LocalStorageManagerAsync);
    if (d->m_useCache && d->m_pLocalStorageCacheManager) {
        d->m_pLocalStorageCacheManager->installCacheExpiryFunction(checker);
        return true;
    }

    return false;
}

const LocalStorageManager * LocalStorageManagerAsync::localStorageManager()
    const
{
    Q_D(const LocalStorageManagerAsync);
    return d->m_pLocalStorageManager;
}

LocalStorageManager * LocalStorageManagerAsync::localStorageManager()
{
    Q_D(LocalStorageManagerAsync);
    return d->m_pLocalStorageManager;
}

void LocalStorageManagerAsync::init()
{
    Q_D(LocalStorageManagerAsync);

    if (d->m_pLocalStorageManager) {
        delete d->m_pLocalStorageManager;
    }

    d->m_pLocalStorageManager =
        new LocalStorageManager(d->m_account, d->m_startupOptions);

    if (d->m_pLocalStorageCacheManager) {
        delete d->m_pLocalStorageCacheManager;
    }

    d->m_pLocalStorageCacheManager = new LocalStorageCacheManager();

    Q_EMIT initialized();
}

void LocalStorageManagerAsync::onGetUserCountRequest(QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        int count = d->m_pLocalStorageManager->userCount(errorDescription);
        if (count < 0) {
            Q_EMIT getUserCountFailed(errorDescription, requestId);
        }
        else {
            Q_EMIT getUserCountComplete(count, requestId);
        }
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't get user count from the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT getUserCountFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onSwitchUserRequest(
    Account account, LocalStorageManager::StartupOptions startupOptions,
    QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        d->m_pLocalStorageManager->switchUser(account, startupOptions);
    }
    catch (const std::exception & e) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't switch user in the local "
                       "storage: caught exception"));

        errorDescription.details() = QString::fromUtf8(e.what());

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->clear();
        }

        Q_EMIT switchUserFailed(account, errorDescription, requestId);
        return;
    }

    if (d->m_useCache) {
        d->m_pLocalStorageCacheManager->clear();
    }

    Q_EMIT switchUserComplete(account, requestId);
}

void LocalStorageManagerAsync::onAddUserRequest(User user, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->addUser(user, errorDescription);
        if (!res) {
            Q_EMIT addUserFailed(user, errorDescription, requestId);
            return;
        }

        Q_EMIT addUserComplete(user, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't add user to the local storage: "
                       "caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT addUserFailed(user, error, requestId);
    }
}

void LocalStorageManagerAsync::onUpdateUserRequest(User user, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res =
            d->m_pLocalStorageManager->updateUser(user, errorDescription);

        if (!res) {
            Q_EMIT updateUserFailed(user, errorDescription, requestId);
            return;
        }

        Q_EMIT updateUserComplete(user, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't update user within the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT updateUserFailed(user, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindUserRequest(User user, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->findUser(user, errorDescription);
        if (!res) {
            Q_EMIT findUserFailed(user, errorDescription, requestId);
            return;
        }

        Q_EMIT findUserComplete(user, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't find user in the local storage: "
                       "caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT findUserFailed(user, error, requestId);
    }
}

void LocalStorageManagerAsync::onDeleteUserRequest(User user, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res =
            d->m_pLocalStorageManager->deleteUser(user, errorDescription);
        if (!res) {
            Q_EMIT deleteUserFailed(user, errorDescription, requestId);
            return;
        }

        Q_EMIT deleteUserComplete(user, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't mark user as deleted in the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT deleteUserFailed(user, error, requestId);
    }
}

void LocalStorageManagerAsync::onExpungeUserRequest(User user, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res =
            d->m_pLocalStorageManager->expungeUser(user, errorDescription);

        if (!res) {
            Q_EMIT expungeUserFailed(user, errorDescription, requestId);
            return;
        }

        Q_EMIT expungeUserComplete(user, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't expunge user from the local storage: "
                       "caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT expungeUserFailed(user, error, requestId);
    }
}

void LocalStorageManagerAsync::onGetNotebookCountRequest(QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        int count = d->m_pLocalStorageManager->notebookCount(errorDescription);
        if (count < 0) {
            Q_EMIT getNotebookCountFailed(errorDescription, requestId);
        }
        else {
            Q_EMIT getNotebookCountComplete(count, requestId);
        }
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't get notebook count from the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT getNotebookCountFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onAddNotebookRequest(
    Notebook notebook, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res =
            d->m_pLocalStorageManager->addNotebook(notebook, errorDescription);

        if (!res) {
            Q_EMIT addNotebookFailed(notebook, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->cacheNotebook(notebook);
        }

        Q_EMIT addNotebookComplete(notebook, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't add notebook to the local storage: "
                       "caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT addNotebookFailed(notebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onUpdateNotebookRequest(
    Notebook notebook, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->updateNotebook(
            notebook, errorDescription);

        if (!res) {
            Q_EMIT updateNotebookFailed(notebook, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->cacheNotebook(notebook);
        }

        Q_EMIT updateNotebookComplete(notebook, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't update notebook in the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT updateNotebookFailed(notebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindNotebookRequest(
    Notebook notebook, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool foundNotebookInCache = false;
        if (d->m_useCache) {
            bool notebookHasGuid = notebook.hasGuid();
            if (notebookHasGuid || !notebook.localUid().isEmpty()) {
                const QString uid =
                    (notebookHasGuid ? notebook.guid() : notebook.localUid());

                LocalStorageCacheManager::WhichUid wg =
                    (notebookHasGuid ? LocalStorageCacheManager::Guid
                                     : LocalStorageCacheManager::LocalUid);

                const Notebook * pNotebook =
                    d->m_pLocalStorageCacheManager->findNotebook(uid, wg);

                if (pNotebook) {
                    notebook = *pNotebook;
                    foundNotebookInCache = true;
                }
            }
            else if (notebook.hasName() && !notebook.name().isEmpty()) {
                const QString notebookName = notebook.name();

                const Notebook * pNotebook =
                    d->m_pLocalStorageCacheManager->findNotebookByName(
                        notebookName);

                if (pNotebook) {
                    notebook = *pNotebook;
                    foundNotebookInCache = true;
                }
            }
        }

        if (!foundNotebookInCache) {
            bool res = d->m_pLocalStorageManager->findNotebook(
                notebook, errorDescription);

            if (!res) {
                Q_EMIT findNotebookFailed(
                    notebook, errorDescription, requestId);
                return;
            }
        }

        Q_EMIT findNotebookComplete(notebook, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't find notebook within the local storage: "
                       "caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT findNotebookFailed(notebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindDefaultNotebookRequest(
    Notebook notebook, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->findDefaultNotebook(
            notebook, errorDescription);

        if (!res) {
            Q_EMIT findDefaultNotebookFailed(
                notebook, errorDescription, requestId);
            return;
        }

        Q_EMIT findDefaultNotebookComplete(notebook, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't find the default notebook within "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT findDefaultNotebookFailed(notebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindLastUsedNotebookRequest(
    Notebook notebook, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->findLastUsedNotebook(
            notebook, errorDescription);

        if (!res) {
            Q_EMIT findLastUsedNotebookFailed(
                notebook, errorDescription, requestId);
            return;
        }

        Q_EMIT findLastUsedNotebookComplete(notebook, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't find the last used notebook within "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT findLastUsedNotebookFailed(notebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindDefaultOrLastUsedNotebookRequest(
    Notebook notebook, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->findDefaultOrLastUsedNotebook(
            notebook, errorDescription);

        if (!res) {
            Q_EMIT findDefaultOrLastUsedNotebookFailed(
                notebook, errorDescription, requestId);
            return;
        }

        Q_EMIT findDefaultOrLastUsedNotebookComplete(notebook, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't find default or last used notebook "
                       "within the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT findDefaultOrLastUsedNotebookFailed(notebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onListAllNotebooksRequest(
    size_t limit, size_t offset, LocalStorageManager::ListNotebooksOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QString linkedNotebookGuid, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        QList<Notebook> notebooks = d->m_pLocalStorageManager->listAllNotebooks(
            errorDescription, limit, offset, order, orderDirection,
            linkedNotebookGuid);

        if (notebooks.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listAllNotebooksFailed(
                limit, offset, order, orderDirection, linkedNotebookGuid,
                errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            for (const auto & notebook: qAsConst(notebooks)) {
                d->m_pLocalStorageCacheManager->cacheNotebook(notebook);
            }
        }

        Q_EMIT listAllNotebooksComplete(
            limit, offset, order, orderDirection, linkedNotebookGuid, notebooks,
            requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't list all notebooks from the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT listAllNotebooksFailed(
            limit, offset, order, orderDirection, linkedNotebookGuid, error,
            requestId);
    }
}

void LocalStorageManagerAsync::onListAllSharedNotebooksRequest(QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        QList<SharedNotebook> sharedNotebooks =
            d->m_pLocalStorageManager->listAllSharedNotebooks(errorDescription);
        if (sharedNotebooks.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listAllSharedNotebooksFailed(errorDescription, requestId);
            return;
        }

        Q_EMIT listAllSharedNotebooksComplete(sharedNotebooks, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't list all shared notebooks from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT listAllSharedNotebooksFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onListNotebooksRequest(
    LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
    LocalStorageManager::ListNotebooksOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QString linkedNotebookGuid, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        QList<Notebook> notebooks = d->m_pLocalStorageManager->listNotebooks(
            flag, errorDescription, limit, offset, order, orderDirection,
            linkedNotebookGuid);

        if (notebooks.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listNotebooksFailed(
                flag, limit, offset, order, orderDirection, linkedNotebookGuid,
                errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            for (const auto & notebook: qAsConst(notebooks)) {
                d->m_pLocalStorageCacheManager->cacheNotebook(notebook);
            }
        }

        Q_EMIT listNotebooksComplete(
            flag, limit, offset, order, orderDirection, linkedNotebookGuid,
            notebooks, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't list notebooks from the local storage: "
                       "caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT listNotebooksFailed(
            flag, limit, offset, order, orderDirection, linkedNotebookGuid,
            error, requestId);
    }
}

void LocalStorageManagerAsync::onListSharedNotebooksPerNotebookGuidRequest(
    QString notebookGuid, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        QList<SharedNotebook> sharedNotebooks =
            d->m_pLocalStorageManager->listSharedNotebooksPerNotebookGuid(
                notebookGuid, errorDescription);

        if (sharedNotebooks.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listSharedNotebooksPerNotebookGuidFailed(
                notebookGuid, errorDescription, requestId);
            return;
        }

        Q_EMIT listSharedNotebooksPerNotebookGuidComplete(
            notebookGuid, sharedNotebooks, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't list shared notebooks by notebook guid "
                       "from the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT listSharedNotebooksPerNotebookGuidFailed(
            notebookGuid, error, requestId);
    }
}

void LocalStorageManagerAsync::onExpungeNotebookRequest(
    Notebook notebook, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->expungeNotebook(
            notebook, errorDescription);

        if (!res) {
            Q_EMIT expungeNotebookFailed(notebook, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->expungeNotebook(notebook);
        }

        Q_EMIT expungeNotebookComplete(notebook, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't expunge notebook from the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT expungeNotebookFailed(notebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onGetLinkedNotebookCountRequest(QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        int count =
            d->m_pLocalStorageManager->linkedNotebookCount(errorDescription);
        if (count < 0) {
            Q_EMIT getLinkedNotebookCountFailed(errorDescription, requestId);
        }
        else {
            Q_EMIT getLinkedNotebookCountComplete(count, requestId);
        }
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't get linked notebook count from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT getLinkedNotebookCountFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onAddLinkedNotebookRequest(
    LinkedNotebook linkedNotebook, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->addLinkedNotebook(
            linkedNotebook, errorDescription);

        if (!res) {
            Q_EMIT addLinkedNotebookFailed(
                linkedNotebook, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->cacheLinkedNotebook(linkedNotebook);
        }

        Q_EMIT addLinkedNotebookComplete(linkedNotebook, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't add linked notebook to the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT addLinkedNotebookFailed(linkedNotebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onUpdateLinkedNotebookRequest(
    LinkedNotebook linkedNotebook, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->updateLinkedNotebook(
            linkedNotebook, errorDescription);

        if (!res) {
            Q_EMIT updateLinkedNotebookFailed(
                linkedNotebook, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->cacheLinkedNotebook(linkedNotebook);
        }

        Q_EMIT updateLinkedNotebookComplete(linkedNotebook, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't update linked notebook in the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT updateLinkedNotebookFailed(linkedNotebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindLinkedNotebookRequest(
    LinkedNotebook linkedNotebook, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool foundLinkedNotebookInCache = false;
        if (d->m_useCache && linkedNotebook.hasGuid()) {
            const QString guid = linkedNotebook.guid();
            const LinkedNotebook * pLinkedNotebook =
                d->m_pLocalStorageCacheManager->findLinkedNotebook(guid);
            if (pLinkedNotebook) {
                linkedNotebook = *pLinkedNotebook;
                foundLinkedNotebookInCache = true;
            }
        }

        if (!foundLinkedNotebookInCache) {
            bool res = d->m_pLocalStorageManager->findLinkedNotebook(
                linkedNotebook, errorDescription);

            if (!res) {
                Q_EMIT findLinkedNotebookFailed(
                    linkedNotebook, errorDescription, requestId);
                return;
            }
        }

        Q_EMIT findLinkedNotebookComplete(linkedNotebook, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't find linked notebook within "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT findLinkedNotebookFailed(linkedNotebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onListAllLinkedNotebooksRequest(
    size_t limit, size_t offset,
    LocalStorageManager::ListLinkedNotebooksOrder order,
    LocalStorageManager::OrderDirection orderDirection, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        QList<LinkedNotebook> linkedNotebooks =
            d->m_pLocalStorageManager->listAllLinkedNotebooks(
                errorDescription, limit, offset, order, orderDirection);

        if (linkedNotebooks.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listAllLinkedNotebooksFailed(
                limit, offset, order, orderDirection, errorDescription,
                requestId);
            return;
        }

        if (d->m_useCache) {
            for (const auto & linkedNotebook: qAsConst(linkedNotebooks)) {
                d->m_pLocalStorageCacheManager->cacheLinkedNotebook(
                    linkedNotebook);
            }
        }

        Q_EMIT listAllLinkedNotebooksComplete(
            limit, offset, order, orderDirection, linkedNotebooks, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't list all linked notebooks from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT listAllLinkedNotebooksFailed(
            limit, offset, order, orderDirection, error, requestId);
    }
}

void LocalStorageManagerAsync::onListLinkedNotebooksRequest(
    LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
    LocalStorageManager::ListLinkedNotebooksOrder order,
    LocalStorageManager::OrderDirection orderDirection, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        QList<LinkedNotebook> linkedNotebooks =
            d->m_pLocalStorageManager->listLinkedNotebooks(
                flag, errorDescription, limit, offset, order, orderDirection);

        if (linkedNotebooks.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listLinkedNotebooksFailed(
                flag, limit, offset, order, orderDirection, errorDescription,
                requestId);
            return;
        }

        if (d->m_useCache) {
            for (const auto & linkedNotebook: qAsConst(linkedNotebooks)) {
                d->m_pLocalStorageCacheManager->cacheLinkedNotebook(
                    linkedNotebook);
            }
        }

        Q_EMIT listLinkedNotebooksComplete(
            flag, limit, offset, order, orderDirection, linkedNotebooks,
            requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't list linked notebooks from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT listLinkedNotebooksFailed(
            flag, limit, offset, order, orderDirection, error, requestId);
    }
}

void LocalStorageManagerAsync::onExpungeLinkedNotebookRequest(
    LinkedNotebook linkedNotebook, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->expungeLinkedNotebook(
            linkedNotebook, errorDescription);

        if (!res) {
            Q_EMIT expungeLinkedNotebookFailed(
                linkedNotebook, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->expungeLinkedNotebook(
                linkedNotebook);
        }

        Q_EMIT expungeLinkedNotebookComplete(linkedNotebook, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't expunge linked notebook from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT expungeLinkedNotebookFailed(linkedNotebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onGetNoteCountRequest(
    LocalStorageManager::NoteCountOptions options, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        int count =
            d->m_pLocalStorageManager->noteCount(errorDescription, options);

        if (count < 0) {
            Q_EMIT getNoteCountFailed(errorDescription, options, requestId);
        }
        else {
            Q_EMIT getNoteCountComplete(count, options, requestId);
        }
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't get note count from the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT getNoteCountFailed(error, options, requestId);
    }
}

void LocalStorageManagerAsync::onGetNoteCountPerNotebookRequest(
    Notebook notebook, LocalStorageManager::NoteCountOptions options,
    QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        int count = d->m_pLocalStorageManager->noteCountPerNotebook(
            notebook, errorDescription, options);

        if (count < 0) {
            Q_EMIT getNoteCountPerNotebookFailed(
                errorDescription, notebook, options, requestId);
        }
        else {
            Q_EMIT getNoteCountPerNotebookComplete(
                count, notebook, options, requestId);
        }
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't get note count per notebook from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT getNoteCountPerNotebookFailed(
            error, notebook, options, requestId);
    }
}

void LocalStorageManagerAsync::onGetNoteCountPerTagRequest(
    Tag tag, LocalStorageManager::NoteCountOptions options, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        int count = d->m_pLocalStorageManager->noteCountPerTag(
            tag, errorDescription, options);

        if (count < 0) {
            Q_EMIT getNoteCountPerTagFailed(
                errorDescription, tag, options, requestId);
        }
        else {
            Q_EMIT getNoteCountPerTagComplete(count, tag, options, requestId);
        }
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't get note count per tag from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT getNoteCountPerTagFailed(error, tag, options, requestId);
    }
}

void LocalStorageManagerAsync::onGetNoteCountsPerAllTagsRequest(
    LocalStorageManager::NoteCountOptions options, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        QHash<QString, int> noteCountsPerTagLocalUid;
        bool res = d->m_pLocalStorageManager->noteCountsPerAllTags(
            noteCountsPerTagLocalUid, errorDescription, options);

        if (!res) {
            Q_EMIT getNoteCountsPerAllTagsFailed(
                errorDescription, options, requestId);
        }
        else {
            Q_EMIT getNoteCountsPerAllTagsComplete(
                noteCountsPerTagLocalUid, options, requestId);
        }
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't get note counts per all tags from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT getNoteCountsPerAllTagsFailed(error, options, requestId);
    }
}

void LocalStorageManagerAsync::onGetNoteCountPerNotebooksAndTagsRequest(
    QStringList notebookLocalUids, QStringList tagLocalUids,
    LocalStorageManager::NoteCountOptions options, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        int count = d->m_pLocalStorageManager->noteCountPerNotebooksAndTags(
            notebookLocalUids, tagLocalUids, errorDescription, options);

        if (count < 0) {
            Q_EMIT getNoteCountPerNotebooksAndTagsFailed(
                errorDescription, notebookLocalUids, tagLocalUids, options,
                requestId);
        }
        else {
            Q_EMIT getNoteCountPerNotebooksAndTagsComplete(
                count, notebookLocalUids, tagLocalUids, options, requestId);
        }
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't get note count per notebooks and "
                       "tags from the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT getNoteCountPerNotebooksAndTagsFailed(
            error, notebookLocalUids, tagLocalUids, options, requestId);
    }
}

void LocalStorageManagerAsync::onAddNoteRequest(Note note, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->addNote(note, errorDescription);
        if (!res) {
            Q_EMIT addNoteFailed(note, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            Note noteForCaching = note;
            QList<Resource> resourcesForCaching;
            splitNoteAndResourcesForCaching(
                noteForCaching, resourcesForCaching);

            d->m_pLocalStorageCacheManager->cacheNote(noteForCaching);

            for (const auto & resource: qAsConst(resourcesForCaching)) {
                d->m_pLocalStorageCacheManager->cacheResource(resource);
            }
        }

        Q_EMIT addNoteComplete(note, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't add note to the local storage: "
                       "caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT addNoteFailed(note, error, requestId);
    }
}

void LocalStorageManagerAsync::onUpdateNoteRequest(
    Note note, const LocalStorageManager::UpdateNoteOptions options,
    QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        bool shouldCheckForNotebookChange = false;
        bool shouldCheckForTagListUpdate = false;

        static const QMetaMethod noteMovedToAnotherNotebookSignal =
            QMetaMethod::fromSignal(
                &LocalStorageManagerAsync::noteMovedToAnotherNotebook);
        if (isSignalConnected(noteMovedToAnotherNotebookSignal)) {
            shouldCheckForNotebookChange = true;
        }

        if (options & LocalStorageManager::UpdateNoteOption::UpdateTags) {
            static const QMetaMethod noteTagListChangedSignal =
                QMetaMethod::fromSignal(
                    &LocalStorageManagerAsync::noteTagListChanged);
            if (isSignalConnected(noteTagListChangedSignal)) {
                shouldCheckForTagListUpdate = true;
            }
        }

        Note previousNoteVersion;
        if (shouldCheckForNotebookChange || shouldCheckForTagListUpdate) {
            bool foundNoteInCache = false;
            if (d->m_useCache) {
                const Note * pNote = nullptr;

                if (note.hasGuid()) {
                    pNote = d->m_pLocalStorageCacheManager->findNote(
                        note.guid(), LocalStorageCacheManager::WhichUid::Guid);
                }

                if (!pNote) {
                    pNote = d->m_pLocalStorageCacheManager->findNote(
                        note.localUid(),
                        LocalStorageCacheManager::WhichUid::LocalUid);
                }

                if (pNote) {
                    previousNoteVersion = *pNote;
                    foundNoteInCache = true;
                }
            }

            if (!foundNoteInCache) {
                ErrorString errorDescription;
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
                LocalStorageManager::GetNoteOptions getNoteOptions;
#else
                LocalStorageManager::GetNoteOptions getNoteOptions(0);
#endif
                bool res = false;

                if (note.hasGuid()) {
                    // Try to find note by guid first
                    previousNoteVersion.setGuid(note.guid());
                    res = d->m_pLocalStorageManager->findNote(
                        previousNoteVersion, getNoteOptions, errorDescription);
                }

                if (!res) {
                    previousNoteVersion.setLocalUid(note.localUid());
                    previousNoteVersion.setGuid(QString());
                    res = d->m_pLocalStorageManager->findNote(
                        previousNoteVersion, getNoteOptions, errorDescription);
                }

                if (!res) {
                    Q_EMIT updateNoteFailed(
                        note, options, errorDescription, requestId);
                    return;
                }
            }
        }

        ErrorString errorDescription;
        bool res = d->m_pLocalStorageManager->updateNote(
            note, options, errorDescription);

        if (!res) {
            Q_EMIT updateNoteFailed(note, options, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            if ((options &
                 LocalStorageManager::UpdateNoteOption::
                     UpdateResourceMetadata) &&
                (options & LocalStorageManager::UpdateNoteOption::UpdateTags))
            {
                Note noteForCaching = note;
                QList<Resource> resourcesForCaching;
                splitNoteAndResourcesForCaching(
                    noteForCaching, resourcesForCaching);

                d->m_pLocalStorageCacheManager->cacheNote(noteForCaching);

                if (options &
                    LocalStorageManager::UpdateNoteOption::
                        UpdateResourceBinaryData) {
                    for (const auto & resource: qAsConst(resourcesForCaching)) {
                        d->m_pLocalStorageCacheManager->cacheResource(resource);
                    }
                }
                else {
                    // Since resources metadata might have changed, it would
                    // become stale within the cache so need to remove it from
                    // there
                    for (const auto & resource: qAsConst(resourcesForCaching)) {
                        d->m_pLocalStorageCacheManager->expungeResource(
                            resource);
                    }
                }
            }
            else {
                // The note was somehow changed but the resources or tags
                // information was not updated => the note in the cache is
                // stale/incomplete in either case, need to remove it from there
                d->m_pLocalStorageCacheManager->expungeNote(note);

                // Same goes for its resources
                QList<Resource> resources = note.resources();
                for (const auto & resource: qAsConst(resources)) {
                    d->m_pLocalStorageCacheManager->expungeResource(resource);
                }
            }
        }

        Q_EMIT updateNoteComplete(note, options, requestId);

        if (shouldCheckForNotebookChange) {
            bool notebookChanged = false;
            if (note.hasNotebookGuid() && previousNoteVersion.hasNotebookGuid())
            {
                notebookChanged =
                    (note.notebookGuid() != previousNoteVersion.notebookGuid());
            }
            else {
                notebookChanged =
                    (note.notebookLocalUid() !=
                     previousNoteVersion.notebookLocalUid());
            }

            if (notebookChanged) {
                QNDEBUG(
                    "local_storage",
                    "Notebook change detected for note "
                        << note.localUid() << ": moved from notebook "
                        << previousNoteVersion.notebookLocalUid()
                        << " to notebook " << note.notebookLocalUid());

                Q_EMIT noteMovedToAnotherNotebook(
                    note.localUid(), previousNoteVersion.notebookLocalUid(),
                    note.notebookLocalUid());
            }
        }

        if (shouldCheckForTagListUpdate) {
            const QStringList & previousTagLocalUids =
                previousNoteVersion.tagLocalUids();

            const QStringList & updatedTagLocalUids = note.tagLocalUids();

            bool tagListUpdated =
                (previousTagLocalUids.size() != updatedTagLocalUids.size());

            if (!tagListUpdated) {
                for (const auto & prevTagLocalUid:
                     qAsConst(previousTagLocalUids)) {
                    int index = updatedTagLocalUids.indexOf(prevTagLocalUid);
                    if (index < 0) {
                        tagListUpdated = true;
                        break;
                    }
                }
            }

            if (tagListUpdated) {
                QNDEBUG(
                    "local_storage",
                    "Tags list update detected for note "
                        << note.localUid() << ": previous tag local uids: "
                        << previousTagLocalUids.join(QStringLiteral(", "))
                        << "; updated tag local uids: "
                        << updatedTagLocalUids.join(QStringLiteral(",")));

                Q_EMIT noteTagListChanged(
                    note.localUid(), previousTagLocalUids, updatedTagLocalUids);
            }
        }
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't update note in the local storage: "
                       "caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT updateNoteFailed(note, options, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindNoteRequest(
    Note note, LocalStorageManager::GetNoteOptions options, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool foundNoteInCache = false;
        if (d->m_useCache) {
            bool noteHasGuid = note.hasGuid();
            const QString uid = (noteHasGuid ? note.guid() : note.localUid());
            LocalStorageCacheManager::WhichUid wu =
                (noteHasGuid ? LocalStorageCacheManager::Guid
                             : LocalStorageCacheManager::LocalUid);

            const Note * pNote =
                d->m_pLocalStorageCacheManager->findNote(uid, wu);

            if (pNote) {
                note = *pNote;
                foundNoteInCache = true;

                if (options &
                    LocalStorageManager::GetNoteOption::WithResourceBinaryData)
                {
                    QList<Resource> resources = note.resources();
                    for (auto & resource: resources) {
                        bool resourceHasGuid = resource.hasGuid();

                        const QString resourceUid =
                            (resourceHasGuid ? resource.guid()
                                             : resource.localUid());

                        LocalStorageCacheManager::WhichUid rwu =
                            (resourceHasGuid
                                 ? LocalStorageCacheManager::Guid
                                 : LocalStorageCacheManager::LocalUid);

                        const Resource * pResource =
                            d->m_pLocalStorageCacheManager->findResource(
                                resourceUid, rwu);

                        if (pResource) {
                            resource = *pResource;
                        }
                        else {
                            LocalStorageManager::GetResourceOptions
                                resourceOptions = LocalStorageManager::
                                    GetResourceOption::WithBinaryData;

                            bool res =
                                d->m_pLocalStorageManager->findEnResource(
                                    resource, resourceOptions,
                                    errorDescription);

                            if (!res) {
                                Q_EMIT findNoteFailed(
                                    note, options, errorDescription, requestId);
                                return;
                            }
                        }
                    }

                    note.setResources(resources);
                }
            }
        }

        if (!foundNoteInCache) {
            bool res = d->m_pLocalStorageManager->findNote(
                note, options, errorDescription);

            if (!res) {
                Q_EMIT findNoteFailed(
                    note, options, errorDescription, requestId);
                return;
            }
        }

        if (!foundNoteInCache && d->m_useCache) {
            QList<Resource> resources = note.resources();
            for (auto & resource: resources) {
                resource.setDataBody(QByteArray());
                resource.setAlternateDataBody(QByteArray());
            }

            Note noteWithoutResourceBinaryData = note;
            noteWithoutResourceBinaryData.setResources(resources);
            d->m_pLocalStorageCacheManager->cacheNote(
                noteWithoutResourceBinaryData);
        }

        if (foundNoteInCache &&
            !(options &
              LocalStorageManager::GetNoteOption::WithResourceMetadata))
        {
            note.setResources(QList<Resource>());
        }

        Q_EMIT findNoteComplete(note, options, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't find note within the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT findNoteFailed(note, options, error, requestId);
    }
}

void LocalStorageManagerAsync::onListNotesPerNotebookRequest(
    Notebook notebook, LocalStorageManager::GetNoteOptions options,
    LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
    LocalStorageManager::ListNotesOrder order,
    LocalStorageManager::OrderDirection orderDirection, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        QList<Note> notes = d->m_pLocalStorageManager->listNotesPerNotebook(
            notebook, options, errorDescription, flag, limit, offset, order,
            orderDirection);

        if (notes.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listNotesPerNotebookFailed(
                notebook, options, flag, limit, offset, order, orderDirection,
                errorDescription, requestId);
            return;
        }

        d->cacheNotes(notes, options);

        Q_EMIT listNotesPerNotebookComplete(
            notebook, options, flag, limit, offset, order, orderDirection,
            notes, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't list notes per notebook from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT listNotesPerNotebookFailed(
            notebook, options, flag, limit, offset, order, orderDirection,
            error, requestId);
    }
}

void LocalStorageManagerAsync::onListNotesPerTagRequest(
    Tag tag, LocalStorageManager::GetNoteOptions options,
    LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
    LocalStorageManager::ListNotesOrder order,
    LocalStorageManager::OrderDirection orderDirection, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        QList<Note> notes = d->m_pLocalStorageManager->listNotesPerTag(
            tag, options, errorDescription, flag, limit, offset, order,
            orderDirection);

        if (notes.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listNotesPerTagFailed(
                tag, options, flag, limit, offset, order, orderDirection,
                errorDescription, requestId);
            return;
        }

        d->cacheNotes(notes, options);
        Q_EMIT listNotesPerTagComplete(
            tag, options, flag, limit, offset, order, orderDirection, notes,
            requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't list notes per tag from the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT listNotesPerTagFailed(
            tag, options, flag, limit, offset, order, orderDirection, error,
            requestId);
    }
}

void LocalStorageManagerAsync::onListNotesPerNotebooksAndTagsRequest(
    QStringList notebookLocalUids, QStringList tagLocalUids,
    LocalStorageManager::GetNoteOptions options,
    LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
    LocalStorageManager::ListNotesOrder order,
    LocalStorageManager::OrderDirection orderDirection, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        QList<Note> notes =
            d->m_pLocalStorageManager->listNotesPerNotebooksAndTags(
                notebookLocalUids, tagLocalUids, options, errorDescription,
                flag, limit, offset, order, orderDirection);

        if (notes.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listNotesPerNotebooksAndTagsFailed(
                notebookLocalUids, tagLocalUids, options, flag, limit, offset,
                order, orderDirection, errorDescription, requestId);
            return;
        }

        d->cacheNotes(notes, options);
        Q_EMIT listNotesPerNotebooksAndTagsComplete(
            notebookLocalUids, tagLocalUids, options, flag, limit, offset,
            order, orderDirection, notes, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't list notes per notebooks and tags "
                       "from the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT listNotesPerNotebooksAndTagsFailed(
            notebookLocalUids, tagLocalUids, options, flag, limit, offset,
            order, orderDirection, error, requestId);
    }
}

void LocalStorageManagerAsync::onListNotesByLocalUidsRequest(
    QStringList noteLocalUids, LocalStorageManager::GetNoteOptions options,
    LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
    LocalStorageManager::ListNotesOrder order,
    LocalStorageManager::OrderDirection orderDirection, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        QList<Note> notes = d->m_pLocalStorageManager->listNotesByLocalUids(
            noteLocalUids, options, errorDescription, flag, limit, offset,
            order, orderDirection);

        if (notes.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listNotesByLocalUidsFailed(
                noteLocalUids, options, flag, limit, offset, order,
                orderDirection, errorDescription, requestId);
            return;
        }

        d->cacheNotes(notes, options);
        Q_EMIT listNotesByLocalUidsComplete(
            noteLocalUids, options, flag, limit, offset, order, orderDirection,
            notes, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't list notes by local uids from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT listNotesByLocalUidsFailed(
            noteLocalUids, options, flag, limit, offset, order, orderDirection,
            error, requestId);
    }
}

void LocalStorageManagerAsync::onListNotesRequest(
    LocalStorageManager::ListObjectsOptions flag,
    LocalStorageManager::GetNoteOptions options, size_t limit, size_t offset,
    LocalStorageManager::ListNotesOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QString linkedNotebookGuid, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        QList<Note> notes = d->m_pLocalStorageManager->listNotes(
            flag, options, errorDescription, limit, offset, order,
            orderDirection, linkedNotebookGuid);

        if (notes.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listNotesFailed(
                flag, options, limit, offset, order, orderDirection,
                linkedNotebookGuid, errorDescription, requestId);
            return;
        }

        d->cacheNotes(notes, options);

        Q_EMIT listNotesComplete(
            flag, options, limit, offset, order, orderDirection,
            linkedNotebookGuid, notes, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't list notes from the local storage: "
                       "caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT listNotesFailed(
            flag, options, limit, offset, order, orderDirection,
            linkedNotebookGuid, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindNoteLocalUidsWithSearchQuery(
    NoteSearchQuery noteSearchQuery, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        QStringList noteLocalUids =
            d->m_pLocalStorageManager->findNoteLocalUidsWithSearchQuery(
                noteSearchQuery, errorDescription);

        if (noteLocalUids.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT findNoteLocalUidsWithSearchQueryFailed(
                noteSearchQuery, errorDescription, requestId);
            return;
        }

        Q_EMIT findNoteLocalUidsWithSearchQueryComplete(
            noteLocalUids, noteSearchQuery, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't find note local uids with search query "
                       "within the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT findNoteLocalUidsWithSearchQueryFailed(
            noteSearchQuery, error, requestId);
    }
}

void LocalStorageManagerAsync::onExpungeNoteRequest(Note note, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        QList<Resource> resources = note.resources();

        bool res =
            d->m_pLocalStorageManager->expungeNote(note, errorDescription);

        if (!res) {
            Q_EMIT expungeNoteFailed(note, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->expungeNote(note);

            for (const auto & resource: qAsConst(resources)) {
                d->m_pLocalStorageCacheManager->expungeResource(resource);
            }
        }

        Q_EMIT expungeNoteComplete(note, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't expunge note from the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT expungeNoteFailed(note, error, requestId);
    }
}

void LocalStorageManagerAsync::onGetTagCountRequest(QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        int count = d->m_pLocalStorageManager->tagCount(errorDescription);
        if (count < 0) {
            Q_EMIT getTagCountFailed(errorDescription, requestId);
        }
        else {
            Q_EMIT getTagCountComplete(count, requestId);
        }
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't get tag count from the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT getTagCountFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onAddTagRequest(Tag tag, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->addTag(tag, errorDescription);
        if (!res) {
            Q_EMIT addTagFailed(tag, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->cacheTag(tag);
        }

        Q_EMIT addTagComplete(tag, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't add tag to the local storage: "
                       "caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT addTagFailed(tag, error, requestId);
    }
}

void LocalStorageManagerAsync::onUpdateTagRequest(Tag tag, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->updateTag(tag, errorDescription);
        if (!res) {
            Q_EMIT updateTagFailed(tag, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->cacheTag(tag);
        }

        Q_EMIT updateTagComplete(tag, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't update tag in the local storage: "
                       "caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT updateTagFailed(tag, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindTagRequest(Tag tag, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool foundTagInCache = false;
        if (d->m_useCache) {
            bool tagHasGuid = tag.hasGuid();
            if (tagHasGuid || !tag.localUid().isEmpty()) {
                const QString uid = (tagHasGuid ? tag.guid() : tag.localUid());
                LocalStorageCacheManager::WhichUid wg =
                    (tagHasGuid ? LocalStorageCacheManager::Guid
                                : LocalStorageCacheManager::LocalUid);

                const Tag * pTag =
                    d->m_pLocalStorageCacheManager->findTag(uid, wg);

                if (pTag) {
                    tag = *pTag;
                    foundTagInCache = true;
                }
            }
            else if (tag.hasName() && !tag.name().isEmpty()) {
                const QString tagName = tag.name();
                const Tag * pTag =
                    d->m_pLocalStorageCacheManager->findTagByName(tagName);
                if (pTag) {
                    tag = *pTag;
                    foundTagInCache = true;
                }
            }
        }

        if (!foundTagInCache) {
            bool res =
                d->m_pLocalStorageManager->findTag(tag, errorDescription);

            if (!res) {
                Q_EMIT findTagFailed(tag, errorDescription, requestId);
                return;
            }
        }

        Q_EMIT findTagComplete(tag, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't find tag within the local storage: "
                       "caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT findTagFailed(tag, error, requestId);
    }
}

void LocalStorageManagerAsync::onListAllTagsPerNoteRequest(
    Note note, LocalStorageManager::ListObjectsOptions flag, size_t limit,
    size_t offset, LocalStorageManager::ListTagsOrder order,
    LocalStorageManager::OrderDirection orderDirection, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        QList<Tag> tags = d->m_pLocalStorageManager->listAllTagsPerNote(
            note, errorDescription, flag, limit, offset, order, orderDirection);

        if (tags.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listAllTagsPerNoteFailed(
                note, flag, limit, offset, order, orderDirection,
                errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            for (const auto & tag: qAsConst(tags)) {
                d->m_pLocalStorageCacheManager->cacheTag(tag);
            }
        }

        Q_EMIT listAllTagsPerNoteComplete(
            tags, note, flag, limit, offset, order, orderDirection, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't list all tags per note from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT listAllTagsPerNoteFailed(
            note, flag, limit, offset, order, orderDirection, error, requestId);
    }
}

void LocalStorageManagerAsync::onListAllTagsRequest(
    size_t limit, size_t offset, LocalStorageManager::ListTagsOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QString linkedNotebookGuid, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        QList<Tag> tags = d->m_pLocalStorageManager->listAllTags(
            errorDescription, limit, offset, order, orderDirection,
            linkedNotebookGuid);

        if (tags.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listAllTagsFailed(
                limit, offset, order, orderDirection, linkedNotebookGuid,
                errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            for (const auto & tag: qAsConst(tags)) {
                d->m_pLocalStorageCacheManager->cacheTag(tag);
            }
        }

        Q_EMIT listAllTagsComplete(
            limit, offset, order, orderDirection, linkedNotebookGuid, tags,
            requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't list all tags from the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT listAllTagsFailed(
            limit, offset, order, orderDirection, linkedNotebookGuid, error,
            requestId);
    }
}

void LocalStorageManagerAsync::onListTagsRequest(
    LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
    LocalStorageManager::ListTagsOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QString linkedNotebookGuid, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        QList<Tag> tags = d->m_pLocalStorageManager->listTags(
            flag, errorDescription, limit, offset, order, orderDirection,
            linkedNotebookGuid);

        if (tags.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listTagsFailed(
                flag, limit, offset, order, orderDirection, linkedNotebookGuid,
                errorDescription, requestId);
        }

        if (d->m_useCache) {
            for (const auto & tag: qAsConst(tags)) {
                d->m_pLocalStorageCacheManager->cacheTag(tag);
            }
        }

        Q_EMIT listTagsComplete(
            flag, limit, offset, order, orderDirection, linkedNotebookGuid,
            tags, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't list tags from the local storage: "
                       "caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT listTagsFailed(
            flag, limit, offset, order, orderDirection, linkedNotebookGuid,
            error, requestId);
    }
}

void LocalStorageManagerAsync::onListTagsWithNoteLocalUidsRequest(
    LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
    LocalStorageManager::ListTagsOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QString linkedNotebookGuid, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        QList<std::pair<Tag, QStringList>> tagsWithNoteLocalUids =
            d->m_pLocalStorageManager->listTagsWithNoteLocalUids(
                flag, errorDescription, limit, offset, order, orderDirection,
                linkedNotebookGuid);

        if (tagsWithNoteLocalUids.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listTagsWithNoteLocalUidsFailed(
                flag, limit, offset, order, orderDirection, linkedNotebookGuid,
                errorDescription, requestId);
        }

        if (d->m_useCache) {
            // clang-format off
            SAVE_WARNINGS
            CLANG_SUPPRESS_WARNING(-Wrange-loop-analysis)
            // clang-format off
            for (const auto it: // clazy:exclude=range-loop
                 qevercloud::toRange(qAsConst(tagsWithNoteLocalUids))) {
                d->m_pLocalStorageCacheManager->cacheTag(it->first);
            }
            RESTORE_WARNINGS
        }

        Q_EMIT listTagsWithNoteLocalUidsComplete(
            flag, limit, offset, order, orderDirection, linkedNotebookGuid,
            tagsWithNoteLocalUids, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't list tags with note local uids from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT listTagsWithNoteLocalUidsFailed(
            flag, limit, offset, order, orderDirection, linkedNotebookGuid,
            error, requestId);
    }
}

void LocalStorageManagerAsync::onExpungeTagRequest(Tag tag, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        QStringList expungedChildTagLocalUids;
        bool res = d->m_pLocalStorageManager->expungeTag(
            tag, expungedChildTagLocalUids, errorDescription);

        if (!res) {
            Q_EMIT expungeTagFailed(tag, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->expungeTag(tag);

            for (const auto & localUid: qAsConst(expungedChildTagLocalUids)) {
                Tag dummyTag;
                dummyTag.setLocalUid(localUid);
                d->m_pLocalStorageCacheManager->expungeTag(dummyTag);
            }
        }

        Q_EMIT expungeTagComplete(tag, expungedChildTagLocalUids, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't expunge tag from the local storage: "
                       "caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT expungeTagFailed(tag, error, requestId);
    }
}

void LocalStorageManagerAsync::onExpungeNotelessTagsFromLinkedNotebooksRequest(
    QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        bool res =
            d->m_pLocalStorageManager->expungeNotelessTagsFromLinkedNotebooks(
                errorDescription);

        if (!res) {
            Q_EMIT expungeNotelessTagsFromLinkedNotebooksFailed(
                errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->clearAllNotes();
            d->m_pLocalStorageCacheManager->clearAllResources();
        }

        Q_EMIT expungeNotelessTagsFromLinkedNotebooksComplete(requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't expunge noteless tags from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT expungeNotelessTagsFromLinkedNotebooksFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onGetResourceCountRequest(QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        int count =
            d->m_pLocalStorageManager->enResourceCount(errorDescription);
        if (count < 0) {
            Q_EMIT getResourceCountFailed(errorDescription, requestId);
        }
        else {
            Q_EMIT getResourceCountComplete(count, requestId);
        }
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't get resource count from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT getResourceCountFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onAddResourceRequest(
    Resource resource, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->addEnResource(
            resource, errorDescription);

        if (!res) {
            Q_EMIT addResourceFailed(resource, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->cacheResource(resource);
        }

        Q_EMIT addResourceComplete(resource, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't add resource to the local storage: "
                       "caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT addResourceFailed(resource, error, requestId);
    }
}

void LocalStorageManagerAsync::onUpdateResourceRequest(
    Resource resource, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->updateEnResource(
            resource, errorDescription);

        if (!res) {
            Q_EMIT updateResourceFailed(resource, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->cacheResource(resource);
        }

        Q_EMIT updateResourceComplete(resource, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't update resource in the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT updateResourceFailed(resource, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindResourceRequest(
    Resource resource, LocalStorageManager::GetResourceOptions options,
    QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool foundResourceInCache = false;
        if (d->m_useCache) {
            bool resourceHasGuid = resource.hasGuid();
            const QString uid =
                (resourceHasGuid ? resource.guid() : resource.localUid());
            LocalStorageCacheManager::WhichUid wu =
                (resourceHasGuid ? LocalStorageCacheManager::Guid
                                 : LocalStorageCacheManager::LocalUid);

            const Resource * pResource =
                d->m_pLocalStorageCacheManager->findResource(uid, wu);
            if (pResource) {
                resource = *pResource;
                foundResourceInCache = true;
            }
        }

        if (!foundResourceInCache) {
            bool res = d->m_pLocalStorageManager->findEnResource(
                resource, options, errorDescription);

            if (!res) {
                Q_EMIT findResourceFailed(
                    resource, options, errorDescription, requestId);
                return;
            }

            if (d->m_useCache &&
                (options &
                 LocalStorageManager::GetResourceOption::WithBinaryData))
            {
                d->m_pLocalStorageCacheManager->cacheResource(resource);
            }
        }
        else if (!(options &
                   LocalStorageManager::GetResourceOption::WithBinaryData)) {
            resource.setDataBody(QByteArray());
            resource.setAlternateDataBody(QByteArray());
        }

        Q_EMIT findResourceComplete(resource, options, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't find resource within the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT findResourceFailed(resource, options, error, requestId);
    }
}

void LocalStorageManagerAsync::onExpungeResourceRequest(
    Resource resource, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->expungeEnResource(
            resource, errorDescription);

        if (!res) {
            Q_EMIT expungeResourceFailed(resource, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->expungeResource(resource);
        }

        Q_EMIT expungeResourceComplete(resource, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't expunge resource from the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT expungeResourceFailed(resource, error, requestId);
    }
}

void LocalStorageManagerAsync::onGetSavedSearchCountRequest(QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        int count =
            d->m_pLocalStorageManager->savedSearchCount(errorDescription);
        if (count < 0) {
            Q_EMIT getSavedSearchCountFailed(errorDescription, requestId);
        }
        else {
            Q_EMIT getSavedSearchCountComplete(count, requestId);
        }
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't get saved searches count from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT getSavedSearchCountFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onAddSavedSearchRequest(
    SavedSearch search, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res =
            d->m_pLocalStorageManager->addSavedSearch(search, errorDescription);

        if (!res) {
            Q_EMIT addSavedSearchFailed(search, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->cacheSavedSearch(search);
        }

        Q_EMIT addSavedSearchComplete(search, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't add saved search to the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT addSavedSearchFailed(search, error, requestId);
    }
}

void LocalStorageManagerAsync::onUpdateSavedSearchRequest(
    SavedSearch search, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->updateSavedSearch(
            search, errorDescription);

        if (!res) {
            Q_EMIT updateSavedSearchFailed(search, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->cacheSavedSearch(search);
        }

        Q_EMIT updateSavedSearchComplete(search, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't update saved search in the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT updateSavedSearchFailed(search, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindSavedSearchRequest(
    SavedSearch search, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool foundCachedSavedSearch = false;
        if (d->m_useCache) {
            bool searchHasGuid = search.hasGuid();
            if (searchHasGuid || !search.localUid().isEmpty()) {
                const QString uid =
                    (searchHasGuid ? search.guid() : search.localUid());
                const LocalStorageCacheManager::WhichUid wg =
                    (searchHasGuid ? LocalStorageCacheManager::Guid
                                   : LocalStorageCacheManager::LocalUid);

                const SavedSearch * pSearch =
                    d->m_pLocalStorageCacheManager->findSavedSearch(uid, wg);
                if (pSearch) {
                    search = *pSearch;
                    foundCachedSavedSearch = true;
                }
            }
            else if (search.hasName() && !search.name().isEmpty()) {
                const QString searchName = search.name();
                const SavedSearch * pSearch =
                    d->m_pLocalStorageCacheManager->findSavedSearchByName(
                        searchName);
                if (pSearch) {
                    search = *pSearch;
                    foundCachedSavedSearch = true;
                }
            }
        }

        if (!foundCachedSavedSearch) {
            bool res = d->m_pLocalStorageManager->findSavedSearch(
                search, errorDescription);

            if (!res) {
                Q_EMIT findSavedSearchFailed(
                    search, errorDescription, requestId);
                return;
            }
        }

        Q_EMIT findSavedSearchComplete(search, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't find saved search within the local "
                       "storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT findSavedSearchFailed(search, error, requestId);
    }
}

void LocalStorageManagerAsync::onListAllSavedSearchesRequest(
    size_t limit, size_t offset,
    LocalStorageManager::ListSavedSearchesOrder order,
    LocalStorageManager::OrderDirection orderDirection, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        QList<SavedSearch> savedSearches =
            d->m_pLocalStorageManager->listAllSavedSearches(
                errorDescription, limit, offset, order, orderDirection);

        if (savedSearches.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listAllSavedSearchesFailed(
                limit, offset, order, orderDirection, errorDescription,
                requestId);
            return;
        }

        if (d->m_useCache) {
            for (const auto & savedSearch: qAsConst(savedSearches)) {
                d->m_pLocalStorageCacheManager->cacheSavedSearch(savedSearch);
            }
        }

        Q_EMIT listAllSavedSearchesComplete(
            limit, offset, order, orderDirection, savedSearches, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't list all saved searches from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT listAllSavedSearchesFailed(
            limit, offset, order, orderDirection, error, requestId);
    }
}

void LocalStorageManagerAsync::onListSavedSearchesRequest(
    LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
    LocalStorageManager::ListSavedSearchesOrder order,
    LocalStorageManager::OrderDirection orderDirection, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;
        QList<SavedSearch> savedSearches =
            d->m_pLocalStorageManager->listSavedSearches(
                flag, errorDescription, limit, offset, order, orderDirection);

        if (savedSearches.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listSavedSearchesFailed(
                flag, limit, offset, order, orderDirection, errorDescription,
                requestId);

            return;
        }

        if (d->m_useCache) {
            for (const auto & savedSearch: qAsConst(savedSearches)) {
                d->m_pLocalStorageCacheManager->cacheSavedSearch(savedSearch);
            }
        }

        Q_EMIT listSavedSearchesComplete(
            flag, limit, offset, order, orderDirection, savedSearches,
            requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't list all saved searches from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT listSavedSearchesFailed(
            flag, limit, offset, order, orderDirection, error, requestId);
    }
}

void LocalStorageManagerAsync::onExpungeSavedSearchRequest(
    SavedSearch search, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        bool res = d->m_pLocalStorageManager->expungeSavedSearch(
            search, errorDescription);

        if (!res) {
            Q_EMIT expungeSavedSearchFailed(
                search, errorDescription, requestId);
            return;
        }

        if (d->m_useCache) {
            d->m_pLocalStorageCacheManager->expungeSavedSearch(search);
        }

        Q_EMIT expungeSavedSearchComplete(search, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't expunge saved search from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT expungeSavedSearchFailed(search, error, requestId);
    }
}

void LocalStorageManagerAsync::onAccountHighUsnRequest(
    QString linkedNotebookGuid, QUuid requestId)
{
    Q_D(LocalStorageManagerAsync);

    try {
        ErrorString errorDescription;

        qint32 updateSequenceNumber = d->m_pLocalStorageManager->accountHighUsn(
            linkedNotebookGuid, errorDescription);

        if (updateSequenceNumber < 0) {
            Q_EMIT accountHighUsnFailed(
                linkedNotebookGuid, errorDescription, requestId);
            return;
        }

        Q_EMIT accountHighUsnComplete(
            updateSequenceNumber, linkedNotebookGuid, requestId);
    }
    catch (const std::exception & e) {
        ErrorString error(
            QT_TR_NOOP("Can't get account high USN from "
                       "the local storage: caught exception"));

        error.details() = QString::fromUtf8(e.what());

        SysInfo sysInfo;
        QNERROR(
            "local_storage", error << "; backtrace: " << sysInfo.stackTrace());

        Q_EMIT accountHighUsnFailed(linkedNotebookGuid, error, requestId);
    }
}

} // namespace quentier
