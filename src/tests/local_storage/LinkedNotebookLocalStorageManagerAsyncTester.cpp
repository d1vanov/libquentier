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

#include "LinkedNotebookLocalStorageManagerAsyncTester.h"

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Compat.h>

#include <QDebug>
#include <QThread>

namespace quentier {
namespace test {

LinkedNotebookLocalStorageManagerAsyncTester::
    LinkedNotebookLocalStorageManagerAsyncTester(QObject * parent) :
    QObject(parent)
{}

LinkedNotebookLocalStorageManagerAsyncTester::
    ~LinkedNotebookLocalStorageManagerAsyncTester()
{
    clear();
}

void LinkedNotebookLocalStorageManagerAsyncTester::onInitTestCase()
{
    QString username =
        QStringLiteral("LinkedNotebookLocalStorageManagerAsyncTester");

    qint32 userId = 1;
    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    clear();

    m_pLocalStorageManagerThread = new QThread(this);
    Account account(username, Account::Type::Evernote, userId);

    m_pLocalStorageManagerAsync =
        new LocalStorageManagerAsync(account, startupOptions);

    createConnections();

    m_pLocalStorageManagerAsync->init();
    m_pLocalStorageManagerAsync->moveToThread(m_pLocalStorageManagerThread);

    m_pLocalStorageManagerThread->start();
}

void LinkedNotebookLocalStorageManagerAsyncTester::initialize()
{
    m_initialLinkedNotebook.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000001"));

    m_initialLinkedNotebook.setUpdateSequenceNumber(1);

    m_initialLinkedNotebook.setShareName(
        QStringLiteral("Fake linked notebook share name"));

    m_initialLinkedNotebook.setUsername(
        QStringLiteral("Fake linked notebook username"));

    m_initialLinkedNotebook.setShardId(
        QStringLiteral("Fake linked notebook shard id"));

    m_initialLinkedNotebook.setSharedNotebookGlobalId(
        QStringLiteral("Fake linked notebook shared notebook global id"));

    m_initialLinkedNotebook.setUri(QStringLiteral("Fake linked notebook uri"));

    m_initialLinkedNotebook.setNoteStoreUrl(
        QStringLiteral("Fake linked notebook note store url"));

    m_initialLinkedNotebook.setWebApiUrlPrefix(
        QStringLiteral("Fake linked notebook web api url prefix"));

    m_initialLinkedNotebook.setStack(
        QStringLiteral("Fake linked notebook stack"));

    m_initialLinkedNotebook.setBusinessId(1);

    ErrorString errorDescription;
    if (!m_initialLinkedNotebook.checkParameters(errorDescription)) {
        QNWARNING(
            "tests:local_storage",
            "Found invalid LinkedNotebook: "
                << m_initialLinkedNotebook << ", error: " << errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    m_state = State::STATE_SENT_ADD_REQUEST;
    Q_EMIT addLinkedNotebookRequest(
        m_initialLinkedNotebook, QUuid::createUuid());
}

void LinkedNotebookLocalStorageManagerAsyncTester::
    onGetLinkedNotebookCountCompleted(int count, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

#define HANDLE_WRONG_STATE()                                                   \
    else {                                                                     \
        errorDescription.setBase(                                              \
            "Internal error in LinkedNotebookLocalStorageManagerAsyncTester: " \
            "found wrong state");                                              \
        Q_EMIT failure(errorDescription.nonLocalizedString());                 \
        return;                                                                \
    }

    if (m_state == State::STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST) {
        if (count != 1) {
            errorDescription.setBase(
                "GetLinkedNotebookCount returned result "
                "different from the expected one (1): ");

            errorDescription.details() = QString::number(count);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = State::STATE_SENT_EXPUNGE_REQUEST;

        Q_EMIT expungeLinkedNotebookRequest(
            m_modifiedLinkedNotebook, QUuid::createUuid());
    }
    else if (m_state == State::STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST) {
        if (count != 0) {
            errorDescription.setBase(
                "GetLinkedNotebookCount returned result "
                "different from the expected one (0): ");

            errorDescription.details() = QString::number(count);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        LinkedNotebook extraLinkedNotebook;

        extraLinkedNotebook.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000001"));

        extraLinkedNotebook.setUpdateSequenceNumber(1);
        extraLinkedNotebook.setUsername(QStringLiteral("Extra LinkedNotebook"));

        extraLinkedNotebook.setShareName(
            QStringLiteral("Fake extra linked notebook share name"));

        extraLinkedNotebook.setSharedNotebookGlobalId(
            QStringLiteral("Fake extra linked notebook shared notebook global "
                           "id"));

        extraLinkedNotebook.setShardId(
            QStringLiteral("Fake extra linked notebook shard id"));

        extraLinkedNotebook.setStack(
            QStringLiteral("Fake extra linked notebook stack"));

        extraLinkedNotebook.setNoteStoreUrl(
            QStringLiteral("Fake extra linked notebook note store url"));

        extraLinkedNotebook.setWebApiUrlPrefix(
            QStringLiteral("Fake extra linked notebook web api url prefix"));

        extraLinkedNotebook.setUri(
            QStringLiteral("Fake extra linked notebook uri"));

        m_state = State::STATE_SENT_ADD_EXTRA_LINKED_NOTEBOOK_ONE_REQUEST;
        Q_EMIT addLinkedNotebookRequest(
            extraLinkedNotebook, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void LinkedNotebookLocalStorageManagerAsyncTester::
    onGetLinkedNotebookCountFailed(
        ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void LinkedNotebookLocalStorageManagerAsyncTester::onAddLinkedNotebookCompleted(
    LinkedNotebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == State::STATE_SENT_ADD_REQUEST) {
        if (m_initialLinkedNotebook != notebook) {
            errorDescription.setBase(
                "Internal error in "
                "LinkedNotebookLocalStorageManagerAsyncTester: "
                "notebook in addLinkedNotebookCompleted slot doesn't match "
                "the original LinkedNotebook");
            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_foundLinkedNotebook = LinkedNotebook();
        m_foundLinkedNotebook.setGuid(notebook.guid());

        m_state = State::STATE_SENT_FIND_AFTER_ADD_REQUEST;
        Q_EMIT findLinkedNotebookRequest(
            m_foundLinkedNotebook, QUuid::createUuid());
    }
    else if (m_state == State::STATE_SENT_ADD_EXTRA_LINKED_NOTEBOOK_ONE_REQUEST)
    {
        m_initialLinkedNotebooks << notebook;

        LinkedNotebook extraLinkedNotebook;

        extraLinkedNotebook.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000002"));

        extraLinkedNotebook.setUpdateSequenceNumber(2);

        extraLinkedNotebook.setUsername(
            QStringLiteral("Fake linked notebook username two"));

        extraLinkedNotebook.setShareName(
            QStringLiteral("Fake extra linked notebook share name two"));

        extraLinkedNotebook.setSharedNotebookGlobalId(
            QStringLiteral("Fake extra linked notebook shared notebook global "
                           "id two"));

        extraLinkedNotebook.setShardId(
            QStringLiteral("Fake extra linked notebook shard id two"));

        extraLinkedNotebook.setStack(
            QStringLiteral("Fake extra linked notebook stack two"));

        extraLinkedNotebook.setNoteStoreUrl(
            QStringLiteral("Fake extra linked notebook note store url two"));

        extraLinkedNotebook.setWebApiUrlPrefix(QStringLiteral(
            "Fake extra linked notebook web api url prefix two"));

        extraLinkedNotebook.setUri(
            QStringLiteral("Fake extra linked notebook uri two"));

        m_state = State::STATE_SENT_ADD_EXTRA_LINKED_NOTEBOOK_TWO_REQUEST;
        Q_EMIT addLinkedNotebookRequest(
            extraLinkedNotebook, QUuid::createUuid());
    }
    else if (m_state == State::STATE_SENT_ADD_EXTRA_LINKED_NOTEBOOK_TWO_REQUEST)
    {
        m_initialLinkedNotebooks << notebook;

        m_state = State::STATE_SENT_LIST_LINKED_NOTEBOOKS_REQUEST;
        size_t limit = 0, offset = 0;

        LocalStorageManager::ListLinkedNotebooksOrder order =
            LocalStorageManager::ListLinkedNotebooksOrder::NoOrder;

        LocalStorageManager::OrderDirection orderDirection =
            LocalStorageManager::OrderDirection::Ascending;

        Q_EMIT listAllLinkedNotebooksRequest(
            limit, offset, order, orderDirection, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void LinkedNotebookLocalStorageManagerAsyncTester::onAddLinkedNotebookFailed(
    LinkedNotebook notebook, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", linked notebook: " << notebook);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void LinkedNotebookLocalStorageManagerAsyncTester::
    onUpdateLinkedNotebookCompleted(LinkedNotebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == State::STATE_SENT_UPDATE_REQUEST) {
        if (m_modifiedLinkedNotebook != notebook) {
            errorDescription.setBase(
                "Internal error in "
                "LinkedNotebookLocalStorageManagerAsyncTester: notebook in "
                "onUpdateLinkedNotebookCompleted slot doesn't match "
                "the original modified LinkedNotebook");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = State::STATE_SENT_FIND_AFTER_UPDATE_REQUEST;
        Q_EMIT findLinkedNotebookRequest(
            m_foundLinkedNotebook, QUuid::createUuid());
    }
}

void LinkedNotebookLocalStorageManagerAsyncTester::onUpdateLinkedNotebookFailed(
    LinkedNotebook notebook, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", linked notebook: " << notebook);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void LinkedNotebookLocalStorageManagerAsyncTester::
    onFindLinkedNotebookCompleted(LinkedNotebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == State::STATE_SENT_FIND_AFTER_ADD_REQUEST) {
        if (notebook != m_initialLinkedNotebook) {
            errorDescription.setBase(
                "Added and found linked notebooks in local storage don't "
                "match");

            QNWARNING(
                "tests:local_storage",
                errorDescription
                    << ": LinkedNotebook added to the local storage: "
                    << m_initialLinkedNotebook
                    << "\nLinkedNotebook found in the local storage: "
                    << notebook);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        // Ok, found linked notebook is good, updating it now
        m_modifiedLinkedNotebook = m_initialLinkedNotebook;

        m_modifiedLinkedNotebook.setUpdateSequenceNumber(
            m_initialLinkedNotebook.updateSequenceNumber() + 1);

        m_modifiedLinkedNotebook.setUsername(
            m_initialLinkedNotebook.username() + QStringLiteral("_modified"));

        m_modifiedLinkedNotebook.setStack(
            m_initialLinkedNotebook.stack() + QStringLiteral("_modified"));

        m_modifiedLinkedNotebook.setShareName(
            m_initialLinkedNotebook.shareName() + QStringLiteral("_modified"));

        m_state = State::STATE_SENT_UPDATE_REQUEST;

        Q_EMIT updateLinkedNotebookRequest(
            m_modifiedLinkedNotebook, QUuid::createUuid());
    }
    else if (m_state == State::STATE_SENT_FIND_AFTER_UPDATE_REQUEST) {
        if (notebook != m_modifiedLinkedNotebook) {
            errorDescription.setBase(
                "Updated and found linked notebooks don't match");

            QNWARNING(
                "tests:local_storage",
                errorDescription
                    << ": LinkedNotebook updated in the local storage: "
                    << m_modifiedLinkedNotebook
                    << "\nLinkedNotebook found in the local storage: "
                    << notebook);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = State::STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST;
        Q_EMIT getLinkedNotebookCountRequest(QUuid::createUuid());
    }
    else if (m_state == State::STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST) {
        errorDescription.setBase(
            "Error: found linked notebook which should "
            "have been expunged from the local storage");

        QNWARNING(
            "tests:local_storage",
            errorDescription
                << ": LinkedNotebook expunged from the local storage: "
                << m_modifiedLinkedNotebook
                << "\nLinkedNotebook found in the local storage: " << notebook);

        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }
    HANDLE_WRONG_STATE();
}

void LinkedNotebookLocalStorageManagerAsyncTester::onFindLinkedNotebookFailed(
    LinkedNotebook notebook, ErrorString errorDescription, QUuid requestId)
{
    if (m_state == State::STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST) {
        m_state = State::STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST;
        Q_EMIT getLinkedNotebookCountRequest(QUuid::createUuid());
        return;
    }

    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", linked notebook: " << notebook);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void LinkedNotebookLocalStorageManagerAsyncTester::
    onListAllLinkedNotebooksCompleted(
        size_t limit, size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<LinkedNotebook> linkedNotebooks, QUuid requestId)
{
    Q_UNUSED(limit)
    Q_UNUSED(offset)
    Q_UNUSED(order)
    Q_UNUSED(orderDirection)
    Q_UNUSED(requestId)

    int numInitialLinkedNotebooks = m_initialLinkedNotebooks.size();
    int numFoundLinkedNotebooks = linkedNotebooks.size();

    ErrorString errorDescription;

    if (numInitialLinkedNotebooks != numFoundLinkedNotebooks) {
        errorDescription.setBase(
            "Error: the number of found linked notebooks does not correspond "
            "to the number of original added linked notebooks");

        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    for (const auto & notebook: qAsConst(m_initialLinkedNotebooks)) {
        if (!linkedNotebooks.contains(notebook)) {
            errorDescription.setBase(
                "Error: one of initial linked notebooks was not found within "
                "the found linked notebooks");

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }
    }

    Q_EMIT success();
}

void LinkedNotebookLocalStorageManagerAsyncTester::
    onListAllLinkedNotebooksFailed(
        size_t limit, size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId)
{
    Q_UNUSED(limit)
    Q_UNUSED(offset)
    Q_UNUSED(order)
    Q_UNUSED(orderDirection)

    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void LinkedNotebookLocalStorageManagerAsyncTester::
    onExpungeLinkedNotebookCompleted(LinkedNotebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_modifiedLinkedNotebook != notebook) {
        errorDescription.setBase(
            "Internal error in LinkedNotebookLocalStorageManagerAsyncTester: "
            "linked notebook in onExpungeLinkedNotebookCompleted slot doesn't "
            "match the original expunged LinkedNotebook");

        QNWARNING("tests:local_storage", errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    m_state = State::STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST;

    Q_EMIT findLinkedNotebookRequest(
        m_foundLinkedNotebook, QUuid::createUuid());
}

void LinkedNotebookLocalStorageManagerAsyncTester::
    onExpungeLinkedNotebookFailed(
        LinkedNotebook notebook, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", linked notebook: " << notebook);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void LinkedNotebookLocalStorageManagerAsyncTester::createConnections()
{
    QObject::connect(
        m_pLocalStorageManagerThread, &QThread::finished,
        m_pLocalStorageManagerThread, &QThread::deleteLater);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::initialized,
        this, &LinkedNotebookLocalStorageManagerAsyncTester::initialize);

    // Request --> slot connections
    QObject::connect(
        this,
        &LinkedNotebookLocalStorageManagerAsyncTester::
            getLinkedNotebookCountRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onGetLinkedNotebookCountRequest);

    QObject::connect(
        this,
        &LinkedNotebookLocalStorageManagerAsyncTester::addLinkedNotebookRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onAddLinkedNotebookRequest);

    QObject::connect(
        this,
        &LinkedNotebookLocalStorageManagerAsyncTester::
            updateLinkedNotebookRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateLinkedNotebookRequest);

    QObject::connect(
        this,
        &LinkedNotebookLocalStorageManagerAsyncTester::
            findLinkedNotebookRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onFindLinkedNotebookRequest);

    QObject::connect(
        this,
        &LinkedNotebookLocalStorageManagerAsyncTester::
            listAllLinkedNotebooksRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onListAllLinkedNotebooksRequest);

    QObject::connect(
        this,
        &LinkedNotebookLocalStorageManagerAsyncTester::
            expungeLinkedNotebookRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeLinkedNotebookRequest);

    // Slot <-- result connections
    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::getLinkedNotebookCountComplete, this,
        &LinkedNotebookLocalStorageManagerAsyncTester::
            onGetLinkedNotebookCountCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::getLinkedNotebookCountFailed, this,
        &LinkedNotebookLocalStorageManagerAsyncTester::
            onGetLinkedNotebookCountFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addLinkedNotebookComplete, this,
        &LinkedNotebookLocalStorageManagerAsyncTester::
            onAddLinkedNotebookCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addLinkedNotebookFailed, this,
        &LinkedNotebookLocalStorageManagerAsyncTester::
            onAddLinkedNotebookFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateLinkedNotebookComplete, this,
        &LinkedNotebookLocalStorageManagerAsyncTester::
            onUpdateLinkedNotebookCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateLinkedNotebookFailed, this,
        &LinkedNotebookLocalStorageManagerAsyncTester::
            onUpdateLinkedNotebookFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::findLinkedNotebookComplete, this,
        &LinkedNotebookLocalStorageManagerAsyncTester::
            onFindLinkedNotebookCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::findLinkedNotebookFailed, this,
        &LinkedNotebookLocalStorageManagerAsyncTester::
            onFindLinkedNotebookFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::listAllLinkedNotebooksComplete, this,
        &LinkedNotebookLocalStorageManagerAsyncTester::
            onListAllLinkedNotebooksCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::listAllLinkedNotebooksFailed, this,
        &LinkedNotebookLocalStorageManagerAsyncTester::
            onListAllLinkedNotebooksFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::expungeLinkedNotebookComplete, this,
        &LinkedNotebookLocalStorageManagerAsyncTester::
            onExpungeLinkedNotebookCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::expungeLinkedNotebookFailed, this,
        &LinkedNotebookLocalStorageManagerAsyncTester::
            onExpungeLinkedNotebookFailed);
}

void LinkedNotebookLocalStorageManagerAsyncTester::clear()
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

    m_state = State::STATE_UNINITIALIZED;
}

#undef HANDLE_WRONG_STATE

QDebug & operator<<(
    QDebug & dbg,
    const LinkedNotebookLocalStorageManagerAsyncTester::State state)
{
    using State = LinkedNotebookLocalStorageManagerAsyncTester::State;

    switch (state) {
    case State::STATE_UNINITIALIZED:
        dbg << "Uninitialized";
        break;
    case State::STATE_SENT_ADD_REQUEST:
        dbg << "Sent add request";
        break;
    case State::STATE_SENT_FIND_AFTER_ADD_REQUEST:
        dbg << "Sent find after add request";
        break;
    case State::STATE_SENT_UPDATE_REQUEST:
        dbg << "Sent update request";
        break;
    case State::STATE_SENT_FIND_AFTER_UPDATE_REQUEST:
        dbg << "Sent find after update request";
        break;
    case State::STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST:
        dbg << "Sent get count after update request";
        break;
    case State::STATE_SENT_EXPUNGE_REQUEST:
        dbg << "Sent expunge request";
        break;
    case State::STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST:
        dbg << "Sent find after expunge request";
        break;
    case State::STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST:
        dbg << "Sent get count after expunge request";
        break;
    case State::STATE_SENT_ADD_EXTRA_LINKED_NOTEBOOK_ONE_REQUEST:
        dbg << "Sent add extra linked notebook one request";
        break;
    case State::STATE_SENT_ADD_EXTRA_LINKED_NOTEBOOK_TWO_REQUEST:
        dbg << "Sent add extra linked notebook two request";
        break;
    case State::STATE_SENT_LIST_LINKED_NOTEBOOKS_REQUEST:
        dbg << "Sent list linked notebooks request";
        break;
    default:
        dbg << "Unknown (" << static_cast<qint64>(state) << ")";
        break;
    }

    return dbg;
}

} // namespace test
} // namespace quentier
