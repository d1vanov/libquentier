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

#ifndef LIB_QUENTIER_TESTS_TAG_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
#define LIB_QUENTIER_TESTS_TAG_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H

#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/types/ErrorString.h>

#include <qevercloud/types/Tag.h>

namespace quentier {

class LocalStorageManagerAsync;

namespace test {

class TagLocalStorageManagerAsyncTester final : public QObject
{
    Q_OBJECT
public:
    explicit TagLocalStorageManagerAsyncTester(QObject * parent = nullptr);
    ~TagLocalStorageManagerAsyncTester() override;

public Q_SLOTS:
    void onInitTestCase();

Q_SIGNALS:
    void success();
    void failure(QString errorDescription);

    // private signals:
    void getTagCountRequest(QUuid requestId);
    void addTagRequest(qevercloud::Tag tag, QUuid requestId);
    void updateTagRequest(qevercloud::Tag tag, QUuid requestId);
    void findTagRequest(qevercloud::Tag tag, QUuid requestId);

    void listAllTagsRequest(
        std::size_t limit, std::size_t offset, LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QUuid requestId);

    void expungeTagRequest(qevercloud::Tag tag, QUuid requestId);

private Q_SLOTS:
    void initialize();
    void onGetTagCountCompleted(int count, QUuid requestId);
    void onGetTagCountFailed(ErrorString errorDescription, QUuid requestId);
    void onAddTagCompleted(qevercloud::Tag tag, QUuid requestId);

    void onAddTagFailed(
        qevercloud::Tag tag, ErrorString errorDescription, QUuid requestId);

    void onUpdateTagCompleted(qevercloud::Tag tag, QUuid requestId);

    void onUpdateTagFailed(
        qevercloud::Tag tag, ErrorString errorDescription, QUuid requestId);

    void onFindTagCompleted(qevercloud::Tag tag, QUuid requestId);

    void onFindTagFailed(
        qevercloud::Tag tag, ErrorString errorDescription, QUuid requestId);

    void onListAllTagsCompleted(
        std::size_t limit, std::size_t offset,
        LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QList<qevercloud::Tag> tags,
        QUuid requestId);

    void onListAllTagsFailed(
        std::size_t limit, std::size_t offset,
        LocalStorageManager::ListTagsOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void onExpungeTagCompleted(
        qevercloud::Tag tag, QStringList expungedChildTagLocalUids,
        QUuid requestId);

    void onExpungeTagFailed(
        qevercloud::Tag tag, ErrorString errorDescription, QUuid requestId);

private:
    void createConnections();
    void clear();

    enum State
    {
        STATE_UNINITIALIZED,
        STATE_SENT_ADD_REQUEST,
        STATE_SENT_FIND_AFTER_ADD_REQUEST,
        STATE_SENT_FIND_BY_NAME_AFTER_ADD_REQUEST,
        STATE_SENT_UPDATE_REQUEST,
        STATE_SENT_FIND_AFTER_UPDATE_REQUEST,
        STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST,
        STATE_SENT_DELETE_REQUEST,
        STATE_SENT_EXPUNGE_REQUEST,
        STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST,
        STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST,
        STATE_SENT_ADD_EXTRA_TAG_ONE_REQUEST,
        STATE_SENT_ADD_EXTRA_TAG_TWO_REQUEST,
        STATE_SENT_LIST_TAGS_REQUEST
    };

    State m_state = STATE_UNINITIALIZED;

    LocalStorageManagerAsync * m_pLocalStorageManagerAsync = nullptr;
    QThread * m_pLocalStorageManagerThread = nullptr;

    qevercloud::Tag m_initialTag;
    qevercloud::Tag m_foundTag;
    qevercloud::Tag m_modifiedTag;
    QList<qevercloud::Tag> m_initialTags;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_TAG_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
