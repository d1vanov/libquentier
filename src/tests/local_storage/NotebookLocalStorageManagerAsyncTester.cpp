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

#include "NotebookLocalStorageManagerAsyncTester.h"

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>

#include <QThread>

namespace quentier {
namespace test {

NotebookLocalStorageManagerAsyncTester::NotebookLocalStorageManagerAsyncTester(
    QObject * parent) :
    QObject(parent)
{}

NotebookLocalStorageManagerAsyncTester::
    ~NotebookLocalStorageManagerAsyncTester()
{
    clear();
}

void NotebookLocalStorageManagerAsyncTester::onInitTestCase()
{
    QString username = QStringLiteral("NotebookLocalStorageManagerAsyncTester");

    clear();

    m_pLocalStorageManagerThread = new QThread(this);
    const Account account{username, Account::Type::Evernote, m_userId};

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    m_pLocalStorageManagerAsync =
        new LocalStorageManagerAsync(account, startupOptions);

    createConnections();

    m_pLocalStorageManagerAsync->init();
    m_pLocalStorageManagerAsync->moveToThread(m_pLocalStorageManagerThread);

    m_pLocalStorageManagerThread->setObjectName(QStringLiteral(
        "NotebookLocalStorageManagerAsyncTester-local-storage-thread"));

    m_pLocalStorageManagerThread->start();
}

void NotebookLocalStorageManagerAsyncTester::initialize()
{
    m_initialNotebook.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000047"));

    m_initialNotebook.setUpdateSequenceNum(1);
    m_initialNotebook.setName(QStringLiteral("Fake notebook name"));
    m_initialNotebook.setServiceCreated(1);
    m_initialNotebook.setServiceUpdated(1);
    m_initialNotebook.setDefaultNotebook(true);
    m_initialNotebook.setPublishing(qevercloud::Publishing{});

    m_initialNotebook.mutablePublishing()->setUri(
        QStringLiteral("Fake publishing uri"));

    m_initialNotebook.mutablePublishing()->setOrder(
        qevercloud::NoteSortOrder::CREATED);

    m_initialNotebook.mutablePublishing()->setAscending(true);

    m_initialNotebook.mutablePublishing()->setPublicDescription(
        QStringLiteral("Fake public description"));

    m_initialNotebook.setPublished(true);
    m_initialNotebook.setStack(QStringLiteral("Fake notebook stack"));

    m_initialNotebook.setBusinessNotebook(qevercloud::BusinessNotebook{});

    m_initialNotebook.mutableBusinessNotebook()->setNotebookDescription(
        QStringLiteral("Fake business notebook description"));

    m_initialNotebook.mutableBusinessNotebook()->setPrivilege(
        qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);

    m_initialNotebook.mutableBusinessNotebook()->setRecommended(true);

    qevercloud::SharedNotebook sharedNotebook;
    sharedNotebook.setId(1);
    sharedNotebook.setUserId(m_userId);
    sharedNotebook.setNotebookGuid(m_initialNotebook.guid());
    sharedNotebook.setEmail(QStringLiteral("Fake shared notebook email"));
    sharedNotebook.setServiceCreated(1);
    sharedNotebook.setServiceUpdated(1);

    sharedNotebook.setGlobalId(
        QStringLiteral("Fake shared notebook global id"));

    sharedNotebook.setUsername(QStringLiteral("Fake shared notebook username"));

    sharedNotebook.setPrivilege(
        qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);


    sharedNotebook.setRecipientSettings(
        qevercloud::SharedNotebookRecipientSettings{});

    sharedNotebook.mutableRecipientSettings()->setReminderNotifyEmail(true);
    sharedNotebook.mutableRecipientSettings()->setReminderNotifyInApp(false);

    m_initialNotebook.setSharedNotebooks(
        QList<qevercloud::SharedNotebook>() << sharedNotebook);

    m_state = STATE_SENT_ADD_REQUEST;
    Q_EMIT addNotebookRequest(m_initialNotebook, QUuid::createUuid());
}

void NotebookLocalStorageManagerAsyncTester::onGetNotebookCountCompleted(
    int count, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

#define HANDLE_WRONG_STATE()                                                   \
    else {                                                                     \
        errorDescription.setBase(                                              \
            "Internal error in "                                               \
            "NotebookLocalStorageManagerAsyncTester: "                         \
            "detected wrong state");                                           \
        QNWARNING("tests:local_storage", errorDescription << ": " << m_state); \
        Q_EMIT failure(errorDescription.nonLocalizedString());                 \
    }

    if (m_state == STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST) {
        if (count != 1) {
            errorDescription.setBase(
                "GetNotebookCount returned result "
                "different from the expected one (1)");

            errorDescription.details() = QString::number(count);
            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_FIND_DEFAULT_NOTEBOOK_AFTER_UPDATE;
        Q_EMIT findDefaultNotebookRequest(m_foundNotebook, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST) {
        if (count != 0) {
            errorDescription.setBase(
                "GetNotebookCount returned result different "
                "from the expected one (0)");

            errorDescription.details() = QString::number(count);
            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        qevercloud::Notebook extraNotebook;

        extraNotebook.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000001"));

        extraNotebook.setUpdateSequenceNum(1);
        extraNotebook.setName(QStringLiteral("Fake extra notebook one"));
        extraNotebook.setServiceCreated(1);
        extraNotebook.setServiceUpdated(1);
        extraNotebook.setDefaultNotebook(true);

        extraNotebook.setPublishing(qevercloud::Publishing{});

        extraNotebook.mutablePublishing()->setUri(
            QStringLiteral("Fake publishing uri one"));

        extraNotebook.mutablePublishing()->setOrder(
            qevercloud::NoteSortOrder::CREATED);

        extraNotebook.mutablePublishing()->setAscending(true);

        extraNotebook.mutablePublishing()->setPublicDescription(
            QStringLiteral("Fake public description one"));

        extraNotebook.setStack(QStringLiteral("Fake notebook stack one"));

        extraNotebook.setBusinessNotebook(qevercloud::BusinessNotebook{});
        extraNotebook.mutableBusinessNotebook()->setNotebookDescription(
            QStringLiteral("Fake business notebook description one"));

        extraNotebook.mutableBusinessNotebook()->setPrivilege(
            qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);

        extraNotebook.mutableBusinessNotebook()->setRecommended(true);

        qevercloud::SharedNotebook sharedNotebookOne;
        sharedNotebookOne.setId(1);
        sharedNotebookOne.setUserId(4);
        sharedNotebookOne.setNotebookGuid(extraNotebook.guid());

        sharedNotebookOne.setEmail(
            QStringLiteral("Fake shared notebook email one"));

        sharedNotebookOne.setServiceCreated(1);
        sharedNotebookOne.setServiceUpdated(1);

        sharedNotebookOne.setGlobalId(
            QStringLiteral("Fake shared notebook global id one"));

        sharedNotebookOne.setUsername(
            QStringLiteral("Fake shared notebook username one"));

        sharedNotebookOne.setPrivilege(
            qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);

        sharedNotebookOne.setRecipientSettings(
            qevercloud::SharedNotebookRecipientSettings{});

        sharedNotebookOne.mutableRecipientSettings()->setReminderNotifyEmail(
            true);

        sharedNotebookOne.mutableRecipientSettings()->setReminderNotifyInApp(
            false);

        extraNotebook.setSharedNotebooks(
            QList<qevercloud::SharedNotebook>() << sharedNotebookOne);

        qevercloud::SharedNotebook sharedNotebookTwo;
        sharedNotebookTwo.setId(2);
        sharedNotebookTwo.setUserId(4);
        sharedNotebookTwo.setNotebookGuid(extraNotebook.guid());

        sharedNotebookTwo.setEmail(
            QStringLiteral("Fake shared notebook email two"));

        sharedNotebookTwo.setServiceCreated(1);
        sharedNotebookTwo.setServiceUpdated(1);

        sharedNotebookTwo.setGlobalId(
            QStringLiteral("Fake shared notebook global id two"));

        sharedNotebookTwo.setUsername(
            QStringLiteral("Fake shared notebook username two"));

        sharedNotebookTwo.setPrivilege(
            qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);

        sharedNotebookTwo.setRecipientSettings(
            qevercloud::SharedNotebookRecipientSettings{});

        sharedNotebookTwo.mutableRecipientSettings()->setReminderNotifyEmail(
            false);

        sharedNotebookTwo.mutableRecipientSettings()->setReminderNotifyInApp(
            true);

        extraNotebook.mutableSharedNotebooks()->append(sharedNotebookTwo);

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
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::onAddNotebookCompleted(
    qevercloud::Notebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_ADD_REQUEST) {
        if (m_initialNotebook != notebook) {
            errorDescription.setBase(
                "Internal error in "
                "NotebookLocalStorageManagerAsyncTester: notebook in "
                "onAddNotebookCompleted doesn't match the original Notebook");

            QNWARNING(
                "tests:local_storage",
                errorDescription << "; original notebook: " << m_initialNotebook
                                 << "\nFound notebook: " << notebook);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_foundNotebook = qevercloud::Notebook();
        m_foundNotebook.setLocalId(notebook.localId());

        m_state = STATE_SENT_FIND_AFTER_ADD_REQUEST;
        Q_EMIT findNotebookRequest(m_foundNotebook, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_ADD_EXTRA_NOTEBOOK_ONE_REQUEST) {
        m_initialNotebooks << notebook;

        qevercloud::Notebook extraNotebook;

        extraNotebook.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000002"));

        extraNotebook.setUpdateSequenceNum(2);
        extraNotebook.setName(QStringLiteral("Fake extra notebook two"));
        extraNotebook.setServiceCreated(2);
        extraNotebook.setServiceUpdated(2);
        extraNotebook.setDefaultNotebook(false);

        extraNotebook.setPublishing(qevercloud::Publishing{});

        extraNotebook.mutablePublishing()->setUri(
            QStringLiteral("Fake publishing uri two"));

        extraNotebook.mutablePublishing()->setOrder(
            qevercloud::NoteSortOrder::CREATED);

        extraNotebook.mutablePublishing()->setAscending(false);

        extraNotebook.mutablePublishing()->setPublicDescription(
            QStringLiteral("Fake public description two"));

        extraNotebook.setStack(QStringLiteral("Fake notebook stack two"));

        extraNotebook.setBusinessNotebook(qevercloud::BusinessNotebook{});

        extraNotebook.mutableBusinessNotebook()->setNotebookDescription(
            QStringLiteral("Fake business notebook description two"));

        extraNotebook.mutableBusinessNotebook()->setPrivilege(
            qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);

        extraNotebook.mutableBusinessNotebook()->setRecommended(false);

        qevercloud::SharedNotebook sharedNotebook;
        sharedNotebook.setId(3);
        sharedNotebook.setUserId(4);
        sharedNotebook.setNotebookGuid(extraNotebook.guid());

        sharedNotebook.setEmail(
            QStringLiteral("Fake shared notebook email three"));

        sharedNotebook.setServiceCreated(2);
        sharedNotebook.setServiceUpdated(2);

        sharedNotebook.setGlobalId(
            QStringLiteral("Fake shared notebook global id three"));

        sharedNotebook.setUsername(
            QStringLiteral("Fake shared notebook username three"));

        sharedNotebook.setPrivilege(
            qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);

        sharedNotebook.setRecipientSettings(
            qevercloud::SharedNotebookRecipientSettings{});

        sharedNotebook.mutableRecipientSettings()->setReminderNotifyEmail(true);

        sharedNotebook.mutableRecipientSettings()->setReminderNotifyInApp(
            false);

        m_allInitialSharedNotebooks << sharedNotebook;

        extraNotebook.setSharedNotebooks(
            QList<qevercloud::SharedNotebook>() << sharedNotebook);

        m_state = STATE_SENT_ADD_EXTRA_NOTEBOOK_TWO_REQUEST;
        Q_EMIT addNotebookRequest(extraNotebook, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_ADD_EXTRA_NOTEBOOK_TWO_REQUEST) {
        m_initialNotebooks << notebook;

        m_state = STATE_SENT_LIST_NOTEBOOKS_REQUEST;
        std::size_t limit = 0, offset = 0;

        LocalStorageManager::ListNotebooksOrder order =
            LocalStorageManager::ListNotebooksOrder::NoOrder;

        LocalStorageManager::OrderDirection orderDirection =
            LocalStorageManager::OrderDirection::Ascending;

        Q_EMIT listAllNotebooksRequest(
            limit, offset, order, orderDirection, QString(),
            QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void NotebookLocalStorageManagerAsyncTester::onAddNotebookFailed(
    qevercloud::Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", Notebook: " << notebook);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::onUpdateNotebookCompleted(
    qevercloud::Notebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_UPDATE_REQUEST) {
        if (m_modifiedNotebook != notebook) {
            errorDescription.setBase(
                "Internal error in NotebookLocalStorageManagerAsyncTester: "
                "notebook pointer in onUpdateNotebookCompleted slot doesn't "
                "match the pointer to the original modified Notebook");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_FIND_AFTER_UPDATE_REQUEST;
        Q_EMIT findNotebookRequest(m_foundNotebook, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void NotebookLocalStorageManagerAsyncTester::onUpdateNotebookFailed(
    qevercloud::Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", Notebook: " << notebook);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::onFindNotebookCompleted(
    qevercloud::Notebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_FIND_AFTER_ADD_REQUEST) {
        if (m_initialNotebook != notebook) {
            errorDescription.setBase(
                "Internal error in NotebookLocalStorageManagerAsyncTester: "
                "notebook in onFindNotebookCompleted slot "
                "doesn't match the original Notebook");

            QNWARNING(
                "tests:local_storage",
                errorDescription << "; original notebook: " << m_initialNotebook
                                 << "\nFound notebook: " << notebook);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        // Attempt to find notebook by name now
        qevercloud::Notebook notebookToFindByName;
        notebookToFindByName.setLocalId(QString{});
        notebookToFindByName.setName(m_initialNotebook.name());

        m_state = STATE_SENT_FIND_BY_NAME_AFTER_ADD_REQUEST;
        Q_EMIT findNotebookRequest(notebook, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_BY_NAME_AFTER_ADD_REQUEST) {
        if (m_initialNotebook != notebook) {
            errorDescription.setBase(
                "Added and found by name notebooks in the local storage don't "
                "match");

            QNWARNING(
                "tests:local_storage",
                errorDescription
                    << ": Notebook added to the local storage: "
                    << m_initialNotebook
                    << "\nNotebook found in the local storage: " << notebook);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_foundNotebook = notebook;

        m_state = STATE_SENT_FIND_DEFAULT_NOTEBOOK_AFTER_ADD;
        Q_EMIT findDefaultNotebookRequest(m_foundNotebook, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_AFTER_UPDATE_REQUEST) {
        if (m_modifiedNotebook != notebook) {
            errorDescription.setBase(
                "Internal error in NotebookLocalStorageManagerAsyncTester: "
                "notebook pointer in onFindNotebookCompleted slot doesn't "
                "match the pointer to the original modified Notebook");
            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_modifiedNotebook = notebook;
        m_foundNotebook = notebook;

        m_state = STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST;
        Q_EMIT getNotebookCountRequest(QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST) {
        errorDescription.setBase(
            "Error: found notebook which should have been expunged from "
            "the local storage");

        QNWARNING(
            "tests:local_storage",
            errorDescription << ": Notebook expunged from the local storage: "
                             << m_modifiedNotebook
                             << "\nNotebook found in the local storage: "
                             << m_foundNotebook);

        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }
    HANDLE_WRONG_STATE();
}

void NotebookLocalStorageManagerAsyncTester::onFindNotebookFailed(
    qevercloud::Notebook notebook, ErrorString errorDescription,
    QUuid requestId)
{
    if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST) {
        m_state = STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST;
        Q_EMIT getNotebookCountRequest(QUuid::createUuid());
        return;
    }

    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", Notebook: " << notebook);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::onFindDefaultNotebookCompleted(
    qevercloud::Notebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_FIND_DEFAULT_NOTEBOOK_AFTER_ADD) {
        if (m_foundNotebook != notebook) {
            errorDescription.setBase(
                "Internal error in NotebookLocalStorageManagerAsyncTester: "
                "notebook pointer in onFindDefaultNotebookCompleted slot "
                "doesn't match the pointer to the original added Notebook");
            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (m_foundNotebook != m_initialNotebook) {
            errorDescription.setBase(
                "Added and found notebooks in the local storage don't match");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        // Ok, found notebook is good, modifying it now
        m_modifiedNotebook = m_initialNotebook;

        m_modifiedNotebook.setUpdateSequenceNum(
            m_initialNotebook.updateSequenceNum().value() + 1);

        m_modifiedNotebook.setName(
            m_initialNotebook.name().value() + QStringLiteral("_modified"));

        m_modifiedNotebook.setDefaultNotebook(false);

        m_modifiedNotebook.setServiceUpdated(
            m_initialNotebook.serviceUpdated().value() + 1);

        m_modifiedNotebook.setPublishing(qevercloud::Publishing{});

        m_modifiedNotebook.mutablePublishing()->setUri(
            m_initialNotebook.publishing()->uri().value() +
            QStringLiteral("_modified"));

        m_modifiedNotebook.mutablePublishing()->setAscending(
            !m_initialNotebook.publishing()->ascending().value());

        m_modifiedNotebook.mutablePublishing()->setPublicDescription(
            m_initialNotebook.publishing()->publicDescription().value() +
            QStringLiteral("_modified"));

        m_modifiedNotebook.setStack(
            m_initialNotebook.stack().value() + QStringLiteral("_modified"));

        m_modifiedNotebook.setBusinessNotebook(qevercloud::BusinessNotebook{});
        m_modifiedNotebook.mutableBusinessNotebook()->setNotebookDescription(
            *m_initialNotebook.businessNotebook()->notebookDescription() +
            QStringLiteral("_modified"));

        m_state = STATE_SENT_UPDATE_REQUEST;

        Q_EMIT updateNotebookRequest(
            m_modifiedNotebook, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_DEFAULT_NOTEBOOK_AFTER_UPDATE) {
        errorDescription.setBase(
            "Error: found default notebook which should not have been in "
            "the local storage");

        QNWARNING(
            "tests:local_storage",
            errorDescription << ": Notebook found in the local storage: "
                             << notebook);

        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }
    HANDLE_WRONG_STATE();
}

void NotebookLocalStorageManagerAsyncTester::onFindDefaultNotebookFailed(
    qevercloud::Notebook notebook, ErrorString errorDescription,
    QUuid requestId)
{
    if (m_state == STATE_SENT_FIND_DEFAULT_NOTEBOOK_AFTER_UPDATE) {
        m_state = STATE_SENT_EXPUNGE_REQUEST;

        Q_EMIT expungeNotebookRequest(
            m_modifiedNotebook, QUuid::createUuid());

        return;
    }

    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", Notebook: " << notebook);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::onListAllNotebooksCompleted(
    std::size_t limit, std::size_t offset,
    LocalStorageManager::ListNotebooksOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QString linkedNotebookGuid, QList<qevercloud::Notebook> notebooks,
    QUuid requestId)
{
    Q_UNUSED(limit)
    Q_UNUSED(offset)
    Q_UNUSED(order)
    Q_UNUSED(orderDirection)
    Q_UNUSED(linkedNotebookGuid)
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_initialNotebooks.size() != notebooks.size()) {
        errorDescription.setBase(
            "Sizes of listed and reference notebooks don't match");
        QNWARNING("tests:local_storage", errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    for (const auto & notebook: qAsConst(m_initialNotebooks)) {
        if (!notebooks.contains(notebook)) {
            ErrorString errorDescription(
                "One of initial notebooks is not found within "
                "the listed notebooks");

            QNWARNING(
                "tests:local_storage",
                errorDescription << ", notebook which was not found: "
                                 << notebook);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }
    }

    m_state = STATE_SENT_LIST_ALL_SHARED_NOTEBOOKS_REQUEST;
    Q_EMIT listAllSharedNotebooksRequest(QUuid::createUuid());
}

void NotebookLocalStorageManagerAsyncTester::onListAllNotebooksFailed(
    std::size_t limit, std::size_t offset,
    LocalStorageManager::ListNotebooksOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QString linkedNotebookGuid, ErrorString errorDescription, QUuid requestId)
{
    Q_UNUSED(limit)
    Q_UNUSED(offset)
    Q_UNUSED(order)
    Q_UNUSED(orderDirection)
    Q_UNUSED(linkedNotebookGuid)

    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::onListAllSharedNotebooksCompleted(
    QList<qevercloud::SharedNotebook> sharedNotebooks, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_allInitialSharedNotebooks.size() != sharedNotebooks.size()) {
        errorDescription.setBase(
            "Sizes of listed and reference shared notebooks "
            "don't match");
        QNWARNING("tests:local_storage", errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    for (const auto & sharedNotebook: qAsConst(m_allInitialSharedNotebooks)) {
        if (!sharedNotebooks.contains(sharedNotebook)) {
            errorDescription.setBase(
                "One of initial shared notebooks is not "
                "found within listed shared notebooks");

            QNWARNING(
                "tests:local_storage",
                errorDescription << ", shared notebook which was not found: "
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
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::
    onListSharedNotebooksPerNotebookGuidCompleted(
        QString notebookGuid, QList<qevercloud::SharedNotebook> sharedNotebooks,
        QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_initialSharedNotebooksPerNotebook.size() != sharedNotebooks.size()) {
        errorDescription.setBase(
            "Sizes of listed and reference shared notebooks don't match");

        QNWARNING(
            "tests:local_storage",
            errorDescription << ", notebook guid = " << notebookGuid);

        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    for (const auto & sharedNotebook:
         qAsConst(m_initialSharedNotebooksPerNotebook)) {
        if (!sharedNotebooks.contains(sharedNotebook)) {
            errorDescription.setBase(
                "One of initial shared notebooks is not "
                "found within the listed shared notebooks");

            QNWARNING(
                "tests:local_storage",
                errorDescription << ", shared notebook which was not found: "
                                 << sharedNotebook
                                 << ", notebook guid = " << notebookGuid);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }
    }

    Q_EMIT success();
}

void NotebookLocalStorageManagerAsyncTester::
    onListSharedNotebooksPerNotebookGuidFailed(
        QString notebookGuid, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", notebook guid = " << notebookGuid);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::onExpungeNotebookCompleted(
    qevercloud::Notebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_modifiedNotebook != notebook) {
        errorDescription.setBase(
            "Internal error in NotebookLocalStorageManagerAsyncTester: "
            "notebook pointer in onExpungeNotebookCompleted slot doesn't match "
            "the pointer to the original expunged Notebook");

        QNWARNING("tests:local_storage", errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    m_state = STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST;
    Q_EMIT findNotebookRequest(m_foundNotebook, QUuid::createUuid());
}

void NotebookLocalStorageManagerAsyncTester::onExpungeNotebookFailed(
    qevercloud::Notebook notebook, ErrorString errorDescription,
    QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", Notebook: " << notebook);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NotebookLocalStorageManagerAsyncTester::createConnections()
{
    QObject::connect(
        m_pLocalStorageManagerThread, &QThread::finished,
        m_pLocalStorageManagerThread, &QThread::deleteLater);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::initialized,
        this, &NotebookLocalStorageManagerAsyncTester::initialize);

    // Request --> slot connections
    QObject::connect(
        this, &NotebookLocalStorageManagerAsyncTester::getNotebookCountRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onGetNotebookCountRequest);

    QObject::connect(
        this, &NotebookLocalStorageManagerAsyncTester::addNotebookRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onAddNotebookRequest);

    QObject::connect(
        this, &NotebookLocalStorageManagerAsyncTester::updateNotebookRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateNotebookRequest);

    QObject::connect(
        this, &NotebookLocalStorageManagerAsyncTester::findNotebookRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onFindNotebookRequest);

    QObject::connect(
        this,
        &NotebookLocalStorageManagerAsyncTester::findDefaultNotebookRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onFindDefaultNotebookRequest);

    QObject::connect(
        this, &NotebookLocalStorageManagerAsyncTester::listAllNotebooksRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onListAllNotebooksRequest);

    QObject::connect(
        this,
        &NotebookLocalStorageManagerAsyncTester::listAllSharedNotebooksRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onListAllSharedNotebooksRequest);

    QObject::connect(
        this,
        &NotebookLocalStorageManagerAsyncTester::
            listSharedNotebooksPerNotebookRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onListSharedNotebooksPerNotebookGuidRequest);

    QObject::connect(
        this, &NotebookLocalStorageManagerAsyncTester::expungeNotebookRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeNotebookRequest);

    // Slot <-- result connections
    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::getNotebookCountComplete, this,
        &NotebookLocalStorageManagerAsyncTester::onGetNotebookCountCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::getNotebookCountFailed, this,
        &NotebookLocalStorageManagerAsyncTester::onGetNotebookCountFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addNotebookComplete, this,
        &NotebookLocalStorageManagerAsyncTester::onAddNotebookCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addNotebookFailed, this,
        &NotebookLocalStorageManagerAsyncTester::onAddNotebookFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookComplete, this,
        &NotebookLocalStorageManagerAsyncTester::onUpdateNotebookCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookFailed, this,
        &NotebookLocalStorageManagerAsyncTester::onUpdateNotebookFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::findNotebookComplete, this,
        &NotebookLocalStorageManagerAsyncTester::onFindNotebookCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::findNotebookFailed, this,
        &NotebookLocalStorageManagerAsyncTester::onFindNotebookFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::findDefaultNotebookComplete, this,
        &NotebookLocalStorageManagerAsyncTester::
            onFindDefaultNotebookCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::findDefaultNotebookFailed, this,
        &NotebookLocalStorageManagerAsyncTester::onFindDefaultNotebookFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::listAllNotebooksComplete, this,
        &NotebookLocalStorageManagerAsyncTester::onListAllNotebooksCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::listAllNotebooksFailed, this,
        &NotebookLocalStorageManagerAsyncTester::onListAllNotebooksFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::listAllSharedNotebooksComplete, this,
        &NotebookLocalStorageManagerAsyncTester::
            onListAllSharedNotebooksCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::listAllSharedNotebooksFailed, this,
        &NotebookLocalStorageManagerAsyncTester::
            onListAllSharedNotebooksFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::listSharedNotebooksPerNotebookGuidComplete,
        this,
        &NotebookLocalStorageManagerAsyncTester::
            onListSharedNotebooksPerNotebookGuidCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::listSharedNotebooksPerNotebookGuidFailed,
        this,
        &NotebookLocalStorageManagerAsyncTester::
            onListSharedNotebooksPerNotebookGuidFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNotebookComplete, this,
        &NotebookLocalStorageManagerAsyncTester::onExpungeNotebookCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNotebookFailed, this,
        &NotebookLocalStorageManagerAsyncTester::onExpungeNotebookFailed);
}

void NotebookLocalStorageManagerAsyncTester::clear()
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

#undef HANDLE_WRONG_STATE

} // namespace test
} // namespace quentier
