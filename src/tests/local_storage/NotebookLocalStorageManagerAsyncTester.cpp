/*
 * Copyright 2016-2019 Dmitry Ivanov
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

#include "NotebookLocalStorageManagerAsyncTester.h"
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <QThread>

namespace quentier {
namespace test {

NotebookLocalStorageManagerAsyncTester::NotebookLocalStorageManagerAsyncTester(QObject *parent) :
    QObject(parent),
    m_state(STATE_UNINITIALIZED),
    m_pLocalStorageManagerAsync(Q_NULLPTR),
    m_pLocalStorageManagerThread(Q_NULLPTR),
    m_userId(4),
    m_initialNotebook(),
    m_foundNotebook(),
    m_modifiedNotebook(),
    m_initialNotebooks(),
    m_allInitialSharedNotebooks(),
    m_initialSharedNotebooksPerNotebook()
{}

NotebookLocalStorageManagerAsyncTester::~NotebookLocalStorageManagerAsyncTester()
{
    clear();
}

void NotebookLocalStorageManagerAsyncTester::onInitTestCase()
{
    QString username = QStringLiteral("NotebookLocalStorageManagerAsyncTester");

    clear();

    m_pLocalStorageManagerThread = new QThread(this);
    Account account(username, Account::Type::Evernote, m_userId);
    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);
    m_pLocalStorageManagerAsync =
        new LocalStorageManagerAsync(account, startupOptions);

    createConnections();

    m_pLocalStorageManagerAsync->init();
    m_pLocalStorageManagerAsync->moveToThread(m_pLocalStorageManagerThread);

    m_pLocalStorageManagerThread->setObjectName(
        QStringLiteral("NotebookLocalStorageManagerAsyncTester-local-storage-thread"));
    m_pLocalStorageManagerThread->start();
}

void NotebookLocalStorageManagerAsyncTester::initialize()
{
    m_initialNotebook.clear();
    m_initialNotebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    m_initialNotebook.setUpdateSequenceNumber(1);
    m_initialNotebook.setName(QStringLiteral("Fake notebook name"));
    m_initialNotebook.setCreationTimestamp(1);
    m_initialNotebook.setModificationTimestamp(1);
    m_initialNotebook.setDefaultNotebook(true);
    m_initialNotebook.setLastUsed(false);
    m_initialNotebook.setPublishingUri(QStringLiteral("Fake publishing uri"));
    m_initialNotebook.setPublishingOrder(1);
    m_initialNotebook.setPublishingAscending(true);
    m_initialNotebook.setPublishingPublicDescription(
        QStringLiteral("Fake public description"));
    m_initialNotebook.setPublished(true);
    m_initialNotebook.setStack(QStringLiteral("Fake notebook stack"));
    m_initialNotebook.setBusinessNotebookDescription(
        QStringLiteral("Fake business notebook description"));
    m_initialNotebook.setBusinessNotebookPrivilegeLevel(1);
    m_initialNotebook.setBusinessNotebookRecommended(true);

    SharedNotebook sharedNotebook;
    sharedNotebook.setId(1);
    sharedNotebook.setUserId(m_userId);
    sharedNotebook.setNotebookGuid(m_initialNotebook.guid());
    sharedNotebook.setEmail(QStringLiteral("Fake shared notebook email"));
    sharedNotebook.setCreationTimestamp(1);
    sharedNotebook.setModificationTimestamp(1);
    sharedNotebook.setGlobalId(QStringLiteral("Fake shared notebook global id"));
    sharedNotebook.setUsername(QStringLiteral("Fake shared notebook username"));
    sharedNotebook.setPrivilegeLevel(1);
    sharedNotebook.setReminderNotifyEmail(true);
    sharedNotebook.setReminderNotifyApp(false);

    m_initialNotebook.addSharedNotebook(sharedNotebook);

    ErrorString errorDescription;
    if (!m_initialNotebook.checkParameters(errorDescription)) {
        QNWARNING("Found invalid notebook: " << m_initialNotebook
                  << ", error: " << errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    m_state = STATE_SENT_ADD_REQUEST;
    Q_EMIT addNotebookRequest(m_initialNotebook, QUuid::createUuid());
}

void NotebookLocalStorageManagerAsyncTester::onGetNotebookCountCompleted(
    int count, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

#define HANDLE_WRONG_STATE() \
    else { \
        errorDescription.setBase("Internal error in "\
                                 "NotebookLocalStorageManagerAsyncTester: "\
                                 "found wrong state"); \
        QNWARNING(errorDescription << ": " << m_state); \
        Q_EMIT failure(errorDescription.nonLocalizedString()); \
    }

    if (m_state == STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST)
    {
        if (count != 1) {
            errorDescription.setBase("GetNotebookCount returned result different "
                                     "from the expected one (1)");
            errorDescription.details() = QString::number(count);
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_FIND_DEFAULT_NOTEBOOK_AFTER_UPDATE;
        Q_EMIT findDefaultNotebookRequest(m_foundNotebook, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST)
    {
        if (count != 0) {
            errorDescription.setBase("GetNotebookCount returned result different "
                                     "from the expected one (0)");
            errorDescription.details() = QString::number(count);
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        Notebook extraNotebook;
        extraNotebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000001"));
        extraNotebook.setUpdateSequenceNumber(1);
        extraNotebook.setName(QStringLiteral("Fake extra notebook one"));
        extraNotebook.setCreationTimestamp(1);
        extraNotebook.setModificationTimestamp(1);
        extraNotebook.setDefaultNotebook(true);
        extraNotebook.setLastUsed(false);
        extraNotebook.setPublishingUri(QStringLiteral("Fake publishing uri one"));
        extraNotebook.setPublishingOrder(1);
        extraNotebook.setPublishingAscending(true);
        extraNotebook.setPublishingPublicDescription(
            QStringLiteral("Fake public description one"));
        extraNotebook.setStack(QStringLiteral("Fake notebook stack one"));
        extraNotebook.setBusinessNotebookDescription(
            QStringLiteral("Fake business notebook description one"));
        extraNotebook.setBusinessNotebookPrivilegeLevel(1);
        extraNotebook.setBusinessNotebookRecommended(true);

        SharedNotebook sharedNotebookOne;
        sharedNotebookOne.setId(1);
        sharedNotebookOne.setUserId(4);
        sharedNotebookOne.setNotebookGuid(extraNotebook.guid());
        sharedNotebookOne.setEmail(QStringLiteral("Fake shared notebook email one"));
        sharedNotebookOne.setCreationTimestamp(1);
        sharedNotebookOne.setModificationTimestamp(1);
        sharedNotebookOne.setGlobalId(QStringLiteral("Fake shared notebook global id one"));
        sharedNotebookOne.setUsername(QStringLiteral("Fake shared notebook username one"));
        sharedNotebookOne.setPrivilegeLevel(1);
        sharedNotebookOne.setReminderNotifyEmail(true);
        sharedNotebookOne.setReminderNotifyApp(false);

        extraNotebook.addSharedNotebook(sharedNotebookOne);

        SharedNotebook sharedNotebookTwo;
        sharedNotebookTwo.setId(2);
        sharedNotebookTwo.setUserId(4);
        sharedNotebookTwo.setNotebookGuid(extraNotebook.guid());
        sharedNotebookTwo.setEmail(QStringLiteral("Fake shared notebook email two"));
        sharedNotebookTwo.setCreationTimestamp(1);
        sharedNotebookTwo.setModificationTimestamp(1);
        sharedNotebookTwo.setGlobalId(QStringLiteral("Fake shared notebook global id two"));
        sharedNotebookTwo.setUsername(QStringLiteral("Fake shared notebook username two"));
        sharedNotebookTwo.setPrivilegeLevel(1);
        sharedNotebookTwo.setReminderNotifyEmail(false);
        sharedNotebookTwo.setReminderNotifyApp(true);

        extraNotebook.addSharedNotebook(sharedNotebookTwo);

        m_allInitialSharedNotebooks << sharedNotebookOne;
        m_allInitialSharedNotebooks << sharedNotebookTwo;

        m_initialSharedNotebooksPerNotebook << sharedNotebookOne;
        m_initialSharedNotebooksPerNotebook << sharedNotebookTwo;

        m_state = STATE_SENT_ADD_EXTRA_NOTEBOOK_ONE_REQUEST;
        Q_EMIT addNotebookRequest(extraNotebook, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void NotebookLocalStorageManagerAsyncTester::onGetNotebookCountFailed(
    ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << ", requestId = " << requestId);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::onAddNotebookCompleted(Notebook notebook,
                                                                    QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_ADD_REQUEST)
    {
        if (m_initialNotebook != notebook) {
            errorDescription.setBase("Internal error in "
                                     "NotebookLocalStorageManagerAsyncTester: "
                                     "notebook in onAddNotebookCompleted doesn't "
                                     "match the original Notebook");
            QNWARNING(errorDescription << "; original notebook: "
                      << m_initialNotebook << "\nFound notebook: "
                      << notebook);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_foundNotebook = Notebook();
        m_foundNotebook.setLocalUid(notebook.localUid());

        m_state = STATE_SENT_FIND_AFTER_ADD_REQUEST;
        Q_EMIT findNotebookRequest(m_foundNotebook, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_ADD_EXTRA_NOTEBOOK_ONE_REQUEST)
    {
        m_initialNotebooks << notebook;

        Notebook extraNotebook;
        extraNotebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000002"));
        extraNotebook.setUpdateSequenceNumber(2);
        extraNotebook.setName(QStringLiteral("Fake extra notebook two"));
        extraNotebook.setCreationTimestamp(2);
        extraNotebook.setModificationTimestamp(2);
        extraNotebook.setDefaultNotebook(false);
        extraNotebook.setLastUsed(true);
        extraNotebook.setPublishingUri(QStringLiteral("Fake publishing uri two"));
        extraNotebook.setPublishingOrder(1);
        extraNotebook.setPublishingAscending(false);
        extraNotebook.setPublishingPublicDescription(
            QStringLiteral("Fake public description two"));
        extraNotebook.setStack(QStringLiteral("Fake notebook stack two"));
        extraNotebook.setBusinessNotebookDescription(
            QStringLiteral("Fake business notebook description two"));
        extraNotebook.setBusinessNotebookPrivilegeLevel(1);
        extraNotebook.setBusinessNotebookRecommended(false);

        SharedNotebook sharedNotebook;
        sharedNotebook.setId(3);
        sharedNotebook.setUserId(4);
        sharedNotebook.setNotebookGuid(extraNotebook.guid());
        sharedNotebook.setEmail(QStringLiteral("Fake shared notebook email three"));
        sharedNotebook.setCreationTimestamp(2);
        sharedNotebook.setModificationTimestamp(2);
        sharedNotebook.setGlobalId(QStringLiteral("Fake shared notebook global id three"));
        sharedNotebook.setUsername(QStringLiteral("Fake shared notebook username three"));
        sharedNotebook.setPrivilegeLevel(1);
        sharedNotebook.setReminderNotifyEmail(true);
        sharedNotebook.setReminderNotifyApp(false);

        m_allInitialSharedNotebooks << sharedNotebook;

        extraNotebook.addSharedNotebook(sharedNotebook);

        m_state = STATE_SENT_ADD_EXTRA_NOTEBOOK_TWO_REQUEST;
        Q_EMIT addNotebookRequest(extraNotebook, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_ADD_EXTRA_NOTEBOOK_TWO_REQUEST)
    {
        m_initialNotebooks << notebook;

        m_state = STATE_SENT_LIST_NOTEBOOKS_REQUEST;
        size_t limit = 0, offset = 0;
        LocalStorageManager::ListNotebooksOrder::type order =
            LocalStorageManager::ListNotebooksOrder::NoOrder;
        LocalStorageManager::OrderDirection::type orderDirection =
            LocalStorageManager::OrderDirection::Ascending;
        QString linkedNotebookGuid;
        Q_EMIT listAllNotebooksRequest(limit, offset, order, orderDirection,
                                       linkedNotebookGuid, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void NotebookLocalStorageManagerAsyncTester::onAddNotebookFailed(Notebook notebook,
                                                                 ErrorString errorDescription,
                                                                 QUuid requestId)
{
    QNWARNING(errorDescription << ", requestId = " << requestId
              << ", Notebook: " << notebook);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::onUpdateNotebookCompleted(Notebook notebook,
                                                                       QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_UPDATE_REQUEST)
    {
        if (m_modifiedNotebook != notebook) {
            errorDescription.setBase("Internal error in "
                                     "NotebookLocalStorageManagerAsyncTester: "
                                     "notebook pointer in onUpdateNotebookCompleted "
                                     "slot doesn't match the pointer to "
                                     "the original modified Notebook");
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_FIND_AFTER_UPDATE_REQUEST;
        Q_EMIT findNotebookRequest(m_foundNotebook, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void NotebookLocalStorageManagerAsyncTester::onUpdateNotebookFailed(Notebook notebook,
                                                                    ErrorString errorDescription,
                                                                    QUuid requestId)
{
    QNWARNING(errorDescription << ", requestId = " << requestId
              << ", Notebook: " << notebook);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::onFindNotebookCompleted(Notebook notebook,
                                                                     QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_FIND_AFTER_ADD_REQUEST)
    {
        if (m_initialNotebook != notebook) {
            errorDescription.setBase("Internal error in "
                                     "NotebookLocalStorageManagerAsyncTester: "
                                     "notebook in onFindNotebookCompleted slot "
                                     "doesn't match the original Notebook");
            QNWARNING(errorDescription << "; original notebook: "
                      << m_initialNotebook << "\nFound notebook: "
                      << notebook);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        // Attempt to find notebook by name now
        Notebook notebookToFindByName;
        notebookToFindByName.unsetLocalUid();
        notebookToFindByName.setName(m_initialNotebook.name());

        m_state = STATE_SENT_FIND_BY_NAME_AFTER_ADD_REQUEST;
        Q_EMIT findNotebookRequest(notebook, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_BY_NAME_AFTER_ADD_REQUEST)
    {
        if (m_initialNotebook != notebook) {
            errorDescription.setBase("Added and found by name notebooks "
                                     "in the local storage don't match");
            QNWARNING(errorDescription
                      << ": Notebook added to the local storage: "
                      << m_initialNotebook
                      << "\nNotebook found in the local storage: "
                      << notebook);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_foundNotebook = notebook;

        m_state = STATE_SENT_FIND_DEFAULT_NOTEBOOK_AFTER_ADD;
        Q_EMIT findDefaultNotebookRequest(m_foundNotebook, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_AFTER_UPDATE_REQUEST)
    {
        if (m_modifiedNotebook != notebook) {
            errorDescription.setBase("Internal error in "
                                     "NotebookLocalStorageManagerAsyncTester: "
                                     "notebook pointer in onFindNotebookCompleted "
                                     "slot doesn't match the pointer to the original "
                                     "modified Notebook");
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_modifiedNotebook = notebook;
        m_foundNotebook = notebook;

        m_state = STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST;
        Q_EMIT getNotebookCountRequest(QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST)
    {
        errorDescription.setBase("Error: found notebook which should have been "
                                 "expunged from the local storage");
        QNWARNING(errorDescription
                  << ": Notebook expunged from the local storage: "
                  << m_modifiedNotebook
                  << "\nNotebook found in the local storage: "
                  << m_foundNotebook);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }
    HANDLE_WRONG_STATE();
}

void NotebookLocalStorageManagerAsyncTester::onFindNotebookFailed(Notebook notebook,
                                                                  ErrorString errorDescription,
                                                                  QUuid requestId)
{
    if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST) {
        m_state = STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST;
        Q_EMIT getNotebookCountRequest(QUuid::createUuid());
        return;
    }

    QNWARNING(errorDescription << ", requestId = " << requestId
              << ", Notebook: " << notebook);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::onFindDefaultNotebookCompleted(Notebook notebook,
                                                                            QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_FIND_DEFAULT_NOTEBOOK_AFTER_ADD)
    {
        if (m_foundNotebook != notebook) {
            errorDescription.setBase("Internal error in "
                                     "NotebookLocalStorageManagerAsyncTester: "
                                     "notebook pointer in "
                                     "onFindDefaultNotebookCompleted slot "
                                     "doesn't match the pointer to the original "
                                     "added Notebook");
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (m_foundNotebook != m_initialNotebook) {
            errorDescription.setBase("Added and found notebooks in the local "
                                     "storage don't match");
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_FIND_LAST_USED_NOTEBOOK_AFTER_ADD;
        Q_EMIT findLastUsedNotebookRequest(m_foundNotebook, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_DEFAULT_NOTEBOOK_AFTER_UPDATE)
    {
        errorDescription.setBase("Error: found default notebook which should not "
                                 "have been in the local storage");
        QNWARNING(errorDescription
                  << ": Notebook found in the local storage: "
                  << notebook);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }
    HANDLE_WRONG_STATE();
}

void NotebookLocalStorageManagerAsyncTester::onFindDefaultNotebookFailed(Notebook notebook,
                                                                         ErrorString errorDescription,
                                                                         QUuid requestId)
{
    if (m_state == STATE_SENT_FIND_DEFAULT_NOTEBOOK_AFTER_UPDATE) {
        m_state = STATE_SENT_FIND_LAST_USED_NOTEBOOK_AFTER_UPDATE;
        Q_EMIT findLastUsedNotebookRequest(m_foundNotebook, QUuid::createUuid());
        return;
    }

    QNWARNING(errorDescription << ", requestId = " << requestId
              << ", Notebook: " << notebook);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::onFindLastUsedNotebookCompleted(Notebook notebook,
                                                                             QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_FIND_LAST_USED_NOTEBOOK_AFTER_UPDATE)
    {
        if (m_foundNotebook != notebook) {
            errorDescription.setBase("Internal error in "
                                     "NotebookLocalStorageManagerAsyncTester: "
                                     "notebook pointer in "
                                     "onFindLastUsedNotebookCompleted slot "
                                     "doesn't match the pointer to the original "
                                     "modified Notebook");
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (m_foundNotebook != m_modifiedNotebook) {
            errorDescription.setBase("Updated and found notebooks in the local "
                                     "storage don't match");
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_FIND_DEFAULT_OR_LAST_USED_NOTEBOOK_AFTER_UPDATE;
        Q_EMIT findDefaultOrLastUsedNotebookRequest(m_foundNotebook, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_LAST_USED_NOTEBOOK_AFTER_ADD)
    {
        errorDescription.setBase("Error: found last used notebook which should "
                                 "not have been in the local storage");
        QNWARNING(errorDescription
                  << ": Notebook found in the local storage: "
                  << notebook);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }
    HANDLE_WRONG_STATE();
}

void NotebookLocalStorageManagerAsyncTester::onFindLastUsedNotebookFailed(Notebook notebook,
                                                                          ErrorString errorDescription,
                                                                          QUuid requestId)
{
    if (m_state == STATE_SENT_FIND_LAST_USED_NOTEBOOK_AFTER_ADD)
    {
        m_state = STATE_SENT_FIND_DEFAULT_OR_LAST_USED_NOTEBOOK_AFTER_ADD;

        Q_EMIT findDefaultOrLastUsedNotebookRequest(m_foundNotebook,
                                                    QUuid::createUuid());
        return;
    }

    QNWARNING(errorDescription << ", requestId = " << requestId
              << ", Notebook: " << notebook);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::onFindDefaultOrLastUsedNotebookCompleted(Notebook notebook,
                                                                                      QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if ( (m_state == STATE_SENT_FIND_DEFAULT_OR_LAST_USED_NOTEBOOK_AFTER_ADD) ||
         (m_state == STATE_SENT_FIND_DEFAULT_OR_LAST_USED_NOTEBOOK_AFTER_UPDATE) )
    {
        if (m_foundNotebook != notebook) {
            errorDescription.setBase("Internal error in "
                                     "NotebookLocalStorageManagerAsyncTester: "
                                     "notebook pointer in "
                                     "onFindDefaultOrLastUsedNotebookCompleted "
                                     "slot doesn't match the pointer to the original "
                                     "Notebook");
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (m_state == STATE_SENT_FIND_DEFAULT_OR_LAST_USED_NOTEBOOK_AFTER_ADD)
        {
            if (m_foundNotebook != m_initialNotebook) {
                errorDescription.setBase("Added and found notebooks in the local "
                                         "storage don't match");
                QNWARNING(errorDescription);
                Q_EMIT failure(errorDescription.nonLocalizedString());
                return;
            }

            // Ok, found notebook is good, modifying it now
            m_modifiedNotebook = m_initialNotebook;
            m_modifiedNotebook.setUpdateSequenceNumber(
                m_initialNotebook.updateSequenceNumber() + 1);
            m_modifiedNotebook.setName(
                m_initialNotebook.name() + QStringLiteral("_modified"));
            m_modifiedNotebook.setDefaultNotebook(false);
            m_modifiedNotebook.setLastUsed(true);
            m_modifiedNotebook.setModificationTimestamp(
                m_initialNotebook.modificationTimestamp() + 1);
            m_modifiedNotebook.setPublishingUri(
                m_initialNotebook.publishingUri() + QStringLiteral("_modified"));
            m_modifiedNotebook.setPublishingAscending(
                !m_initialNotebook.isPublishingAscending());
            m_modifiedNotebook.setPublishingPublicDescription(
                m_initialNotebook.publishingPublicDescription() +
                QStringLiteral("_modified"));
            m_modifiedNotebook.setStack(
                m_initialNotebook.stack() + QStringLiteral("_modified"));
            m_modifiedNotebook.setBusinessNotebookDescription(
                m_initialNotebook.businessNotebookDescription() +
                QStringLiteral("_modified"));

            m_state = STATE_SENT_UPDATE_REQUEST;
            Q_EMIT updateNotebookRequest(m_modifiedNotebook, QUuid::createUuid());
        }
        else
        {
            if (m_foundNotebook != m_modifiedNotebook) {
                errorDescription.setBase("Updated and found notebooks "
                                         "in the local storage don't match");
                QNWARNING(errorDescription);
                Q_EMIT failure(errorDescription.nonLocalizedString());
                return;
            }

            m_state = STATE_SENT_EXPUNGE_REQUEST;
            Q_EMIT expungeNotebookRequest(m_modifiedNotebook, QUuid::createUuid());
        }
    }
    HANDLE_WRONG_STATE();
}

void NotebookLocalStorageManagerAsyncTester::onFindDefaultOrLastUsedNotebookFailed(
    Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << ", requestId = " << requestId
              << ", Notebook: " << notebook);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::onListAllNotebooksCompleted(
    size_t limit, size_t offset,
    LocalStorageManager::ListNotebooksOrder::type order,
    LocalStorageManager::OrderDirection::type orderDirection,
    QString linkedNotebookGuid, QList<Notebook> notebooks, QUuid requestId)
{
    Q_UNUSED(limit)
    Q_UNUSED(offset)
    Q_UNUSED(order)
    Q_UNUSED(orderDirection)
    Q_UNUSED(linkedNotebookGuid)
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_initialNotebooks.size() != notebooks.size()) {
        errorDescription.setBase("Sizes of listed and reference notebooks "
                                 "don't match");
        QNWARNING(errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    foreach(const Notebook & notebook, m_initialNotebooks) {
        if (!notebooks.contains(notebook)) {
            ErrorString errorDescription(
                "One of initial notebooks is not found within "
                "the listed notebooks");
            QNWARNING(errorDescription << ", notebook which was not found: "
                      << notebook);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }
    }

    m_state = STATE_SENT_LIST_ALL_SHARED_NOTEBOOKS_REQUEST;
    Q_EMIT listAllSharedNotebooksRequest(QUuid::createUuid());
}

void NotebookLocalStorageManagerAsyncTester::onListAllNotebooksFailed(
    size_t limit, size_t offset,
    LocalStorageManager::ListNotebooksOrder::type order,
    LocalStorageManager::OrderDirection::type orderDirection,
    QString linkedNotebookGuid,
    ErrorString errorDescription, QUuid requestId)
{
    Q_UNUSED(limit)
    Q_UNUSED(offset)
    Q_UNUSED(order)
    Q_UNUSED(orderDirection)
    Q_UNUSED(linkedNotebookGuid)

    QNWARNING(errorDescription << ", requestId = " << requestId);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::onListAllSharedNotebooksCompleted(
    QList<SharedNotebook> sharedNotebooks, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_allInitialSharedNotebooks.size() != sharedNotebooks.size()) {
        errorDescription.setBase("Sizes of listed and reference shared notebooks "
                                 "don't match");
        QNWARNING(errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    for(auto it = m_allInitialSharedNotebooks.constBegin(),
        end = m_allInitialSharedNotebooks.constEnd(); it != end; ++it)
    {
        const SharedNotebook & sharedNotebook = *it;
        if (!sharedNotebooks.contains(sharedNotebook)) {
            errorDescription.setBase("One of initial shared notebooks is not "
                                     "found within listed shared notebooks");
            QNWARNING(errorDescription
                      << ", shared notebook which was not found: "
                      << sharedNotebook);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }
    }

    m_state = STATE_SENT_LIST_SHARED_NOTEBOOKS_PER_NOTEBOOK_REQUEST;
    Q_EMIT listSharedNotebooksPerNotebookRequest(
        QStringLiteral("00000000-0000-0000-c000-000000000001"),
        QUuid::createUuid());
}

void NotebookLocalStorageManagerAsyncTester::onListAllSharedNotebooksFailed(
    ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << ", requestId = " << requestId);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::onListSharedNotebooksPerNotebookGuidCompleted(
    QString notebookGuid, QList<SharedNotebook> sharedNotebooks, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_initialSharedNotebooksPerNotebook.size() != sharedNotebooks.size())
    {
        errorDescription.setBase("Sizes of listed and reference shared notebooks "
                                 "don't match");
        QNWARNING(errorDescription << ", notebook guid = "
                  << notebookGuid);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    for(auto it = m_initialSharedNotebooksPerNotebook.constBegin(),
        end = m_initialSharedNotebooksPerNotebook.constEnd(); it != end; ++it)
    {
        const SharedNotebook & sharedNotebook = *it;
        if (!sharedNotebooks.contains(sharedNotebook))
        {
            errorDescription.setBase("One of initial shared notebooks is not found "
                                     "within the listed shared notebooks");
            QNWARNING(errorDescription << ", shared notebook which was not found: "
                      << sharedNotebook << ", notebook guid = " << notebookGuid);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }
    }

    Q_EMIT success();
}

void NotebookLocalStorageManagerAsyncTester::onListSharedNotebooksPerNotebookGuidFailed(
    QString notebookGuid, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << ", requestId = " << requestId
              << ", notebook guid = " << notebookGuid);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::onExpungeNotebookCompleted(Notebook notebook,
                                                                        QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_modifiedNotebook != notebook)
    {
        errorDescription.setBase("Internal error in NotebookLocalStorageManagerAsyncTester: "
                                 "notebook pointer in onExpungeNotebookCompleted "
                                 "slot doesn't match the pointer to the original "
                                 "expunged Notebook");
        QNWARNING(errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    m_state = STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST;
    Q_EMIT findNotebookRequest(m_foundNotebook, QUuid::createUuid());
}

void NotebookLocalStorageManagerAsyncTester::onExpungeNotebookFailed(Notebook notebook,
                                                                     ErrorString errorDescription,
                                                                     QUuid requestId)
{
    QNWARNING(errorDescription << ", requestId = " << requestId
              << ", Notebook: " << notebook);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::createConnections()
{
    QObject::connect(m_pLocalStorageManagerThread, QNSIGNAL(QThread,finished),
                     m_pLocalStorageManagerThread, QNSLOT(QThread,deleteLater));

    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,initialized),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,initialize));

    // Request --> slot connections
    QObject::connect(this,
                     QNSIGNAL(NotebookLocalStorageManagerAsyncTester,
                              getNotebookCountRequest,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,
                            onGetNotebookCountRequest,QUuid));
    QObject::connect(this,
                     QNSIGNAL(NotebookLocalStorageManagerAsyncTester,
                              addNotebookRequest,Notebook,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,
                            onAddNotebookRequest,Notebook,QUuid));
    QObject::connect(this,
                     QNSIGNAL(NotebookLocalStorageManagerAsyncTester,
                              updateNotebookRequest,Notebook,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,
                            onUpdateNotebookRequest,Notebook,QUuid));
    QObject::connect(this,
                     QNSIGNAL(NotebookLocalStorageManagerAsyncTester,
                              findNotebookRequest,Notebook,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onFindNotebookRequest,
                            Notebook,QUuid));
    QObject::connect(this,
                     QNSIGNAL(NotebookLocalStorageManagerAsyncTester,
                              findDefaultNotebookRequest,Notebook,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onFindDefaultNotebookRequest,
                            Notebook,QUuid));
    QObject::connect(this,
                     QNSIGNAL(NotebookLocalStorageManagerAsyncTester,
                              findLastUsedNotebookRequest,Notebook,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onFindLastUsedNotebookRequest,
                            Notebook,QUuid));
    QObject::connect(this,
                     QNSIGNAL(NotebookLocalStorageManagerAsyncTester,
                              findDefaultOrLastUsedNotebookRequest,Notebook,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,
                            onFindDefaultOrLastUsedNotebookRequest,Notebook,QUuid));
    QObject::connect(this,
                     QNSIGNAL(NotebookLocalStorageManagerAsyncTester,
                              listAllNotebooksRequest,size_t,size_t,
                              LocalStorageManager::ListNotebooksOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onListAllNotebooksRequest,
                            size_t,size_t,LocalStorageManager::ListNotebooksOrder::type,
                            LocalStorageManager::OrderDirection::type,QString,QUuid));
    QObject::connect(this,
                     QNSIGNAL(NotebookLocalStorageManagerAsyncTester,
                              listAllSharedNotebooksRequest,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,
                            onListAllSharedNotebooksRequest,QUuid));
    QObject::connect(this,
                     QNSIGNAL(NotebookLocalStorageManagerAsyncTester,
                              listSharedNotebooksPerNotebookRequest,QString,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,
                            onListSharedNotebooksPerNotebookGuidRequest,QString,QUuid));
    QObject::connect(this,
                     QNSIGNAL(NotebookLocalStorageManagerAsyncTester,
                              expungeNotebookRequest,Notebook,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onExpungeNotebookRequest,
                            Notebook,QUuid));

    // Slot <-- result connections
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,getNotebookCountComplete,
                              int,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onGetNotebookCountCompleted,int,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,getNotebookCountFailed,
                              ErrorString,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onGetNotebookCountFailed,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,addNotebookComplete,
                              Notebook,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onAddNotebookCompleted,Notebook,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,addNotebookFailed,
                              Notebook,ErrorString,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onAddNotebookFailed,Notebook,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateNotebookComplete,
                              Notebook,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onUpdateNotebookCompleted,Notebook,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateNotebookFailed,
                              Notebook,ErrorString,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onUpdateNotebookFailed,Notebook,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,findNotebookComplete,
                              Notebook,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onFindNotebookCompleted,Notebook,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,findNotebookFailed,
                              Notebook,ErrorString,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onFindNotebookFailed,Notebook,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,findDefaultNotebookComplete,
                              Notebook,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onFindDefaultNotebookCompleted,Notebook,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,findDefaultNotebookFailed,
                              Notebook,ErrorString,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onFindDefaultNotebookFailed,Notebook,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,
                              findLastUsedNotebookComplete,Notebook,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onFindLastUsedNotebookCompleted,Notebook,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,findLastUsedNotebookFailed,
                              Notebook,ErrorString,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onFindLastUsedNotebookFailed,Notebook,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,
                              findDefaultOrLastUsedNotebookComplete,Notebook,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onFindDefaultOrLastUsedNotebookCompleted,Notebook,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,
                              findDefaultOrLastUsedNotebookFailed,Notebook,
                              ErrorString,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onFindDefaultOrLastUsedNotebookFailed,
                            Notebook,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listAllNotebooksComplete,
                              size_t,size_t,LocalStorageManager::ListNotebooksOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,QList<Notebook>,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onListAllNotebooksCompleted,size_t,size_t,
                            LocalStorageManager::ListNotebooksOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,QList<Notebook>,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listAllNotebooksFailed,
                              size_t,size_t,LocalStorageManager::ListNotebooksOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,ErrorString,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onListAllNotebooksFailed,size_t,size_t,
                            LocalStorageManager::ListNotebooksOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listAllSharedNotebooksComplete,
                              QList<SharedNotebook>,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onListAllSharedNotebooksCompleted,
                            QList<SharedNotebook>,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,
                              listAllSharedNotebooksFailed,ErrorString,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onListAllSharedNotebooksFailed,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,
                              listSharedNotebooksPerNotebookGuidComplete,
                              QString,QList<SharedNotebook>,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onListSharedNotebooksPerNotebookGuidCompleted,
                            QString,QList<SharedNotebook>,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,
                              listSharedNotebooksPerNotebookGuidFailed,
                              QString,ErrorString,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onListSharedNotebooksPerNotebookGuidFailed,
                            QString,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,expungeNotebookComplete,
                              Notebook,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onExpungeNotebookCompleted,Notebook,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,expungeNotebookFailed,
                              Notebook,ErrorString,QUuid),
                     this,
                     QNSLOT(NotebookLocalStorageManagerAsyncTester,
                            onExpungeNotebookFailed,Notebook,ErrorString,QUuid));
}

void NotebookLocalStorageManagerAsyncTester::clear()
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

#undef HANDLE_WRONG_STATE

} // namespace quentier
} // namespace test
