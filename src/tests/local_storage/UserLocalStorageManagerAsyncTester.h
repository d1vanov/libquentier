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

#ifndef LIB_QUENTIER_TESTS_USER_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
#define LIB_QUENTIER_TESTS_USER_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H

#include <quentier/types/ErrorString.h>

#include <qevercloud/generated/types/User.h>

#include <QUuid>

namespace quentier {

class LocalStorageManagerAsync;

namespace test {

class UserLocalStorageManagerAsyncTester final : public QObject
{
    Q_OBJECT
public:
    explicit UserLocalStorageManagerAsyncTester(QObject * parent = nullptr);
    ~UserLocalStorageManagerAsyncTester() override;

public Q_SLOTS:
    void onInitTestCase();

Q_SIGNALS:
    void success();
    void failure(QString errorDescription);

    // private signals:
    void getUserCountRequest(QUuid requestId);
    void addUserRequest(qevercloud::User user, QUuid requestId);
    void updateUserRequest(qevercloud::User user, QUuid requestId);
    void findUserRequest(qevercloud::User user, QUuid requestId);
    void deleteUserRequest(qevercloud::User user, QUuid requestId);
    void expungeUserRequest(qevercloud::User user, QUuid requestId);

private Q_SLOTS:
    void initialize();
    void onGetUserCountCompleted(int count, QUuid requestId);
    void onGetUserCountFailed(ErrorString errorDescription, QUuid requestId);
    void onAddUserCompleted(qevercloud::User user, QUuid requestId);

    void onAddUserFailed(
        qevercloud::User user, ErrorString errorDescription, QUuid requestId);

    void onUpdateUserCompleted(qevercloud::User user, QUuid requestId);

    void onUpdateUserFailed(
        qevercloud::User user, ErrorString errorDescription, QUuid requestId);

    void onFindUserCompleted(qevercloud::User user, QUuid requestId);

    void onFindUserFailed(
        qevercloud::User user, ErrorString errorDescription, QUuid requestId);

    void onDeleteUserCompleted(qevercloud::User user, QUuid requestId);

    void onDeleteUserFailed(
        qevercloud::User user, ErrorString errorDescription, QUuid requestId);

    void onExpungeUserCompleted(qevercloud::User user, QUuid requestId);

    void onExpungeUserFailed(
        qevercloud::User user, ErrorString errorDescription, QUuid requestId);

private:
    void createConnections();
    void clear();

    enum State
    {
        STATE_UNINITIALIZED,
        STATE_SENT_ADD_REQUEST,
        STATE_SENT_FIND_AFTER_ADD_REQUEST,
        STATE_SENT_UPDATE_REQUEST,
        STATE_SENT_FIND_AFTER_UPDATE_REQUEST,
        STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST,
        STATE_SENT_DELETE_REQUEST,
        STATE_SENT_EXPUNGE_REQUEST,
        STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST,
        STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST
    };

    State m_state = STATE_UNINITIALIZED;

    LocalStorageManagerAsync * m_pLocalStorageManagerAsync = nullptr;
    QThread * m_pLocalStorageManagerThread = nullptr;

    qint32 m_userId = 3;

    qevercloud::User m_initialUser;
    qevercloud::User m_foundUser;
    qevercloud::User m_modifiedUser;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_USER_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
