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

#include "UserLocalStorageManagerAsyncTester.h"
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <QThread>

namespace quentier {
namespace test {

UserLocalStorageManagerAsyncTester::UserLocalStorageManagerAsyncTester(
        QObject * parent) :
    QObject(parent),
    m_state(STATE_UNINITIALIZED),
    m_pLocalStorageManagerAsync(Q_NULLPTR),
    m_pLocalStorageManagerThread(Q_NULLPTR),
    m_userId(3),
    m_initialUser(),
    m_foundUser(),
    m_modifiedUser()
{}

UserLocalStorageManagerAsyncTester::~UserLocalStorageManagerAsyncTester()
{
    clear();
}

void UserLocalStorageManagerAsyncTester::onInitTestCase()
{
    QString username = QStringLiteral("UserLocalStorageManagerAsyncTester");

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
        QStringLiteral("UserLocalStorageManagerAsyncTester-local-storage-thread"));
    m_pLocalStorageManagerThread->start();
}

void UserLocalStorageManagerAsyncTester::initialize()
{
    m_initialUser.setUsername(QStringLiteral("fakeusername"));
    m_initialUser.setId(m_userId);
    m_initialUser.setEmail(QStringLiteral("Fake user email"));
    m_initialUser.setName(QStringLiteral("Fake user name"));
    m_initialUser.setTimezone(QStringLiteral("Europe/Moscow"));
    m_initialUser.setPrivilegeLevel(static_cast<qint8>(qevercloud::PrivilegeLevel::NORMAL));
    m_initialUser.setCreationTimestamp(3);
    m_initialUser.setModificationTimestamp(3);
    m_initialUser.setActive(true);

    ErrorString errorDescription;
    if (!m_initialUser.checkParameters(errorDescription)) {
        QNWARNING("Found invalid user: " << m_initialUser
                  << ", error: " << errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    m_state = STATE_SENT_ADD_REQUEST;
    Q_EMIT addUserRequest(m_initialUser, QUuid::createUuid());
}

void UserLocalStorageManagerAsyncTester::onGetUserCountCompleted(
    int count, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

#define HANDLE_WRONG_STATE()                                                   \
    else                                                                       \
    {                                                                          \
        errorDescription.setBase("Internal error in "                          \
                                 "UserLocalStorageManagerAsyncTester: "        \
                                 "found wrong state");                         \
        Q_EMIT failure(errorDescription.nonLocalizedString());                 \
    }                                                                          \
// HANDLE_WRONG_STATE

    if (m_state == STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST)
    {
        if (count != 1)
        {
            errorDescription.setBase("GetUserCount returned result different "
                                     "from the expected one (1)");
            errorDescription.details() = QString::number(count);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_modifiedUser.setLocal(false);
        m_modifiedUser.setDeletionTimestamp(13);
        m_state = STATE_SENT_DELETE_REQUEST;
        Q_EMIT deleteUserRequest(m_modifiedUser, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST)
    {
        if (count != 0)
        {
            errorDescription.setBase("GetUserCount returned result different "
                                     "from the expected one (0)");
            errorDescription.details() = QString::number(count);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        Q_EMIT success();
    }
    HANDLE_WRONG_STATE();
}

void UserLocalStorageManagerAsyncTester::onGetUserCountFailed(
    ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << ", requestId = " << requestId);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void UserLocalStorageManagerAsyncTester::onAddUserCompleted(
    User user, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_ADD_REQUEST)
    {
        if (m_initialUser != user)
        {
            errorDescription.setBase("Internal error in "
                                     "UserLocalStorageManagerAsyncTester: "
                                     "user in onAddUserCompleted doesn't match "
                                     "the original User");
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_foundUser = User();
        m_foundUser.setId(user.id());

        m_state = STATE_SENT_FIND_AFTER_ADD_REQUEST;
        Q_EMIT findUserRequest(m_foundUser, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void UserLocalStorageManagerAsyncTester::onAddUserFailed(
    User user, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << ", requestId = " << requestId
              << ", user: " << user);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void UserLocalStorageManagerAsyncTester::onUpdateUserCompleted(
    User user, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_UPDATE_REQUEST)
    {
        if (m_modifiedUser != user)
        {
            errorDescription.setBase("Internal error in "
                                     "UserLocalStorageManagerAsyncTester: "
                                     "user in onUpdateUserCompleted slot "
                                     "doesn't match the original modified User");
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_FIND_AFTER_UPDATE_REQUEST;
        Q_EMIT findUserRequest(m_foundUser, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void UserLocalStorageManagerAsyncTester::onUpdateUserFailed(
    User user, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << ", requestId = " << requestId
              << ", user: " << user);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void UserLocalStorageManagerAsyncTester::onFindUserCompleted(
    User user, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_FIND_AFTER_ADD_REQUEST)
    {
        if (user != m_initialUser)
        {
            errorDescription.setBase("Added and found users in "
                                     "the local storage don't match");
            QNWARNING(errorDescription << ": User added to the local storage: "
                      << m_initialUser
                      << "\nUserWrapper found in the local storage: " << user);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        // Ok, found user is good, updating it now
        m_modifiedUser = m_initialUser;
        m_modifiedUser.setUsername(
            m_initialUser.username() + QStringLiteral("_modified"));
        m_modifiedUser.setName(
            m_initialUser.name() + QStringLiteral("_modified"));

        m_state = STATE_SENT_UPDATE_REQUEST;
        Q_EMIT updateUserRequest(m_modifiedUser, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_AFTER_UPDATE_REQUEST)
    {
        if (user != m_modifiedUser)
        {
            errorDescription.setBase("Updated and found users in the local "
                                     "storage don't match");
            QNWARNING(errorDescription << ": User updated in the local storage: "
                      << m_modifiedUser
                      << "\nUserWrapper found in the local storage: " << user);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST;
        Q_EMIT getUserCountRequest(QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST)
    {
        errorDescription.setBase("Error: found user which should have been "
                                 "expunged from local storage");
        QNWARNING(errorDescription << ": User expunged from the local storage: "
                  << m_modifiedUser
                  << "\nUserWrapper found in the local storage: " << user);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }
    HANDLE_WRONG_STATE();
}

void UserLocalStorageManagerAsyncTester::onFindUserFailed(
    User user, ErrorString errorDescription, QUuid requestId)
{
    if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST) {
        m_state = STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST;
        Q_EMIT getUserCountRequest(QUuid::createUuid());
        return;
    }

    QNWARNING(errorDescription << ", requestId = " << requestId
              << ", user: " << user);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void UserLocalStorageManagerAsyncTester::onDeleteUserCompleted(
    User user, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_modifiedUser != user)
    {
        errorDescription.setBase("Internal error in "
                                 "UserLocalStorageManagerAsyncTester: "
                                 "user in onDeleteUserCompleted slot doesn't "
                                 "match the original deleted User");
        QNWARNING(errorDescription << "; original deleted user: "
                  << m_modifiedUser << ", user: " << user);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    m_modifiedUser.setLocal(true);
    m_state = STATE_SENT_EXPUNGE_REQUEST;
    Q_EMIT expungeUserRequest(m_modifiedUser, QUuid::createUuid());
}

void UserLocalStorageManagerAsyncTester::onDeleteUserFailed(
    User user, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << ", requestId = " << requestId
              << ", user: " << user);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void UserLocalStorageManagerAsyncTester::onExpungeUserCompleted(
    User user, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_modifiedUser != user) {
        errorDescription.setBase("Internal error in "
                                 "UserLocalStorageManagerAsyncTester: "
                                 "user in onExpungeUserCompleted slot doesn't "
                                 "match the original expunged User");
        QNWARNING(errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
    }

    m_state = STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST;
    Q_EMIT findUserRequest(m_foundUser, QUuid::createUuid());
}

void UserLocalStorageManagerAsyncTester::onExpungeUserFailed(
    User user, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << ", requestId = " << requestId
              << ", user: " << user);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void UserLocalStorageManagerAsyncTester::createConnections()
{
    QObject::connect(m_pLocalStorageManagerThread, QNSIGNAL(QThread,finished),
                     m_pLocalStorageManagerThread, QNSLOT(QThread,deleteLater));

    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,initialized),
                     this,
                     QNSLOT(UserLocalStorageManagerAsyncTester,initialize));

    // Request --> slot connections
    QObject::connect(this,
                     QNSIGNAL(UserLocalStorageManagerAsyncTester,
                              getUserCountRequest,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,
                            onGetUserCountRequest,QUuid));
    QObject::connect(this,
                     QNSIGNAL(UserLocalStorageManagerAsyncTester,
                              addUserRequest,User,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onAddUserRequest,
                            User,QUuid));
    QObject::connect(this,
                     QNSIGNAL(UserLocalStorageManagerAsyncTester,
                              updateUserRequest,User,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onUpdateUserRequest,
                            User,QUuid));
    QObject::connect(this,
                     QNSIGNAL(UserLocalStorageManagerAsyncTester,
                              findUserRequest,User,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onFindUserRequest,
                            User,QUuid));
    QObject::connect(this,
                     QNSIGNAL(UserLocalStorageManagerAsyncTester,
                              deleteUserRequest,User,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onDeleteUserRequest,
                            User,QUuid));
    QObject::connect(this,
                     QNSIGNAL(UserLocalStorageManagerAsyncTester,
                              expungeUserRequest,User,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onExpungeUserRequest,
                            User,QUuid));

    // Slot <-- result connections
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,getUserCountComplete,
                              int,QUuid),
                     this,
                     QNSLOT(UserLocalStorageManagerAsyncTester,
                            onGetUserCountCompleted,int,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,getUserCountFailed,
                              ErrorString,QUuid),
                     this,
                     QNSLOT(UserLocalStorageManagerAsyncTester,
                            onGetUserCountFailed,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,addUserComplete,
                              User,QUuid),
                     this,
                     QNSLOT(UserLocalStorageManagerAsyncTester,
                            onAddUserCompleted,User,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,addUserFailed,
                              User,ErrorString,QUuid),
                     this,
                     QNSLOT(UserLocalStorageManagerAsyncTester,onAddUserFailed,
                            User,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateUserComplete,
                              User,QUuid),
                     this,
                     QNSLOT(UserLocalStorageManagerAsyncTester,
                            onUpdateUserCompleted,User,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateUserFailed,
                              User,ErrorString,QUuid),
                     this,
                     QNSLOT(UserLocalStorageManagerAsyncTester,
                            onUpdateUserFailed,User,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,findUserComplete,
                              User,QUuid),
                     this,
                     QNSLOT(UserLocalStorageManagerAsyncTester,
                            onFindUserCompleted,User,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,findUserFailed,
                              User,ErrorString,QUuid),
                     this,
                     QNSLOT(UserLocalStorageManagerAsyncTester,onFindUserFailed,
                            User,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,deleteUserComplete,
                              User,QUuid),
                     this,
                     QNSLOT(UserLocalStorageManagerAsyncTester,
                            onDeleteUserCompleted,User,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,deleteUserFailed,
                              User,ErrorString,QUuid),
                     this,
                     QNSLOT(UserLocalStorageManagerAsyncTester,
                            onDeleteUserFailed,User,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,expungeUserComplete,
                              User,QUuid),
                     this,
                     QNSLOT(UserLocalStorageManagerAsyncTester,
                            onExpungeUserCompleted,User,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,expungeUserFailed,
                              User,ErrorString,QUuid),
                     this,
                     QNSLOT(UserLocalStorageManagerAsyncTester,
                            onExpungeUserFailed,User,ErrorString,QUuid));
}

void UserLocalStorageManagerAsyncTester::clear()
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

} // namespace test
} // namespace quentier

