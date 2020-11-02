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

#include "TagLocalStorageManagerAsyncTester.h"

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Compat.h>

#include <QThread>

namespace quentier {
namespace test {

TagLocalStorageManagerAsyncTester::TagLocalStorageManagerAsyncTester(
    QObject * parent) :
    QObject(parent)
{}

TagLocalStorageManagerAsyncTester::~TagLocalStorageManagerAsyncTester()
{
    clear();
}

void TagLocalStorageManagerAsyncTester::onInitTestCase()
{
    QString username = QStringLiteral("TagLocalStorageManagerAsyncTester");
    qint32 userId = 2;

    clear();

    m_pLocalStorageManagerThread = new QThread(this);
    Account account(username, Account::Type::Evernote, userId);

    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    m_pLocalStorageManagerAsync =
        new LocalStorageManagerAsync(account, startupOptions);

    createConnections();

    m_pLocalStorageManagerAsync->init();
    m_pLocalStorageManagerAsync->moveToThread(m_pLocalStorageManagerThread);

    m_pLocalStorageManagerThread->setObjectName(QStringLiteral(
        "TagLocalStorageManagerAsyncTester-local-storage-thread"));

    m_pLocalStorageManagerThread->start();
}

void TagLocalStorageManagerAsyncTester::initialize()
{
    m_initialTag = Tag();

    m_initialTag.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000046"));

    m_initialTag.setUpdateSequenceNumber(3);
    m_initialTag.setName(QStringLiteral("Fake tag name"));

    ErrorString errorDescription;
    if (!m_initialTag.checkParameters(errorDescription)) {
        QNWARNING(
            "tests:local_storage",
            "Found invalid Tag: " << m_initialTag
                                  << ", error: " << errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    m_state = STATE_SENT_ADD_REQUEST;
    Q_EMIT addTagRequest(m_initialTag, QUuid::createUuid());
}

void TagLocalStorageManagerAsyncTester::onGetTagCountCompleted(
    int count, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

#define HANDLE_WRONG_STATE()                                                   \
    else {                                                                     \
        errorDescription.setBase(                                              \
            "Internal error in TagLocalStorageManagerAsyncTester: "            \
            "found wrong state");                                              \
        Q_EMIT failure(errorDescription.nonLocalizedString());                 \
        return;                                                                \
    }

    if (m_state == STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST) {
        if (count != 1) {
            errorDescription.setBase(
                "GetTagCount returned result different "
                "from the expected one (1)");

            errorDescription.details() = QString::number(count);
            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_modifiedTag.setLocal(true);
        m_state = STATE_SENT_EXPUNGE_REQUEST;
        Q_EMIT expungeTagRequest(m_modifiedTag, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST) {
        if (count != 0) {
            errorDescription.setBase(
                "GetTagCount returned result different "
                "from the expected one (0)");

            errorDescription.details() = QString::number(count);
            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        Tag extraTag;

        extraTag.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000001"));

        extraTag.setUpdateSequenceNumber(1);
        extraTag.setName(QStringLiteral("Extra tag name one"));

        m_state = STATE_SENT_ADD_EXTRA_TAG_ONE_REQUEST;
        Q_EMIT addTagRequest(extraTag, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void TagLocalStorageManagerAsyncTester::onGetTagCountFailed(
    ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void TagLocalStorageManagerAsyncTester::onAddTagCompleted(
    Tag tag, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_ADD_REQUEST) {
        if (m_initialTag != tag) {
            errorDescription.setBase(
                "Internal error in TagLocalStorageManagerAsyncTester: "
                "tag in onAddTagCompleted slot doesn't match the original Tag");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_foundTag = Tag();
        m_foundTag.setLocalUid(tag.localUid());

        m_state = STATE_SENT_FIND_AFTER_ADD_REQUEST;
        Q_EMIT findTagRequest(m_foundTag, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_ADD_EXTRA_TAG_ONE_REQUEST) {
        m_initialTags << tag;

        Tag extraTag;

        extraTag.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000002"));

        extraTag.setUpdateSequenceNumber(2);
        extraTag.setName(QStringLiteral("Extra tag name two"));
        extraTag.setParentGuid(tag.guid());

        m_state = STATE_SENT_ADD_EXTRA_TAG_TWO_REQUEST;
        Q_EMIT addTagRequest(extraTag, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_ADD_EXTRA_TAG_TWO_REQUEST) {
        m_initialTags << tag;

        m_state = STATE_SENT_LIST_TAGS_REQUEST;
        size_t limit = 0, offset = 0;
        auto order = LocalStorageManager::ListTagsOrder::NoOrder;
        auto orderDirection = LocalStorageManager::OrderDirection::Ascending;
        QString linkedNotebookGuid;

        Q_EMIT listAllTagsRequest(
            limit, offset, order, orderDirection, linkedNotebookGuid,
            QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void TagLocalStorageManagerAsyncTester::onAddTagFailed(
    Tag tag, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", request id = " << requestId << ", tag: " << tag);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void TagLocalStorageManagerAsyncTester::onUpdateTagCompleted(
    Tag tag, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_UPDATE_REQUEST) {
        if (m_modifiedTag != tag) {
            errorDescription.setBase(
                "Internal error in TagLocalStorageManagerAsyncTester: "
                "tag in onUpdateTagCompleted slot doesn't "
                "match the original modified Tag");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_FIND_AFTER_UPDATE_REQUEST;
        Q_EMIT findTagRequest(m_foundTag, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void TagLocalStorageManagerAsyncTester::onUpdateTagFailed(
    Tag tag, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId << ", tag: " << tag);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void TagLocalStorageManagerAsyncTester::onFindTagCompleted(
    Tag tag, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_FIND_AFTER_ADD_REQUEST) {
        if (tag != m_initialTag) {
            errorDescription.setBase(
                "Added and found tags in the local storage don't match");

            QNWARNING(
                "tests:local_storage",
                errorDescription
                    << ": Tag added to the local storage: " << m_initialTag
                    << "\nTag found in the local storage: " << m_foundTag);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        // Attempt to find tag by name now
        Tag tagToFindByName;
        tagToFindByName.unsetLocalUid();
        tagToFindByName.setName(m_initialTag.name());

        m_state = STATE_SENT_FIND_BY_NAME_AFTER_ADD_REQUEST;
        Q_EMIT findTagRequest(tagToFindByName, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_BY_NAME_AFTER_ADD_REQUEST) {
        if (tag != m_initialTag) {
            errorDescription.setBase(
                "Added and found by name tags in the local "
                "storage don't match");

            QNWARNING(
                "tests:local_storage",
                errorDescription
                    << ": Tag added to the local storage: " << m_initialTag
                    << "\nTag found in the local storage: " << m_foundTag);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        // Ok, found tag is good, updating it now
        m_modifiedTag = m_initialTag;

        m_modifiedTag.setUpdateSequenceNumber(
            m_initialTag.updateSequenceNumber() + 1);

        m_modifiedTag.setName(
            m_initialTag.name() + QStringLiteral("_modified"));

        m_state = STATE_SENT_UPDATE_REQUEST;
        Q_EMIT updateTagRequest(m_modifiedTag, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_AFTER_UPDATE_REQUEST) {
        if (tag != m_modifiedTag) {
            errorDescription.setBase(
                "Updated and found tags in the local storage don't match");

            QNWARNING(
                "tests:local_storage",
                errorDescription
                    << ": Tag updated in the local storage: " << m_modifiedTag
                    << "\nTag found in the local storage: " << m_foundTag);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST;
        Q_EMIT getTagCountRequest(QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST) {
        errorDescription.setBase(
            "Found tag which should have been expunged "
            "from the local storage");

        QNWARNING(
            "tests:local_storage",
            errorDescription
                << ": Tag expunged from the local storage: " << m_modifiedTag
                << "\nTag found in the local storage: " << m_foundTag);

        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }
    HANDLE_WRONG_STATE();
}

void TagLocalStorageManagerAsyncTester::onFindTagFailed(
    Tag tag, ErrorString errorDescription, QUuid requestId)
{
    if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST) {
        m_state = STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST;
        Q_EMIT getTagCountRequest(QUuid::createUuid());
        return;
    }

    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId << ", tag: " << tag);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void TagLocalStorageManagerAsyncTester::onListAllTagsCompleted(
    size_t limit, size_t offset, LocalStorageManager::ListTagsOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QString linkedNotebookGuid, QList<Tag> tags, QUuid requestId)
{
    Q_UNUSED(limit)
    Q_UNUSED(offset)
    Q_UNUSED(order)
    Q_UNUSED(orderDirection)
    Q_UNUSED(linkedNotebookGuid)
    Q_UNUSED(requestId)

    int numInitialTags = m_initialTags.size();
    int numFoundTags = tags.size();

    ErrorString errorDescription;

    if (numInitialTags != numFoundTags) {
        errorDescription.setBase(
            "Error: number of found tags does not "
            "correspond to the number of original added tags");

        QNWARNING("tests:local_storage", errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    for (const auto & tag: qAsConst(m_initialTags)) {
        if (!tags.contains(tag)) {
            errorDescription.setBase(
                "One of initial tags was not found within found tags");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }
    }

    Q_EMIT success();
}

void TagLocalStorageManagerAsyncTester::onListAllTagsFailed(
    size_t limit, size_t offset, LocalStorageManager::ListTagsOrder order,
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

void TagLocalStorageManagerAsyncTester::onExpungeTagCompleted(
    Tag tag, QStringList expungedChildTagLocalUids, QUuid requestId)
{
    Q_UNUSED(requestId)
    Q_UNUSED(expungedChildTagLocalUids)

    ErrorString errorDescription;

    if (m_modifiedTag != tag) {
        errorDescription.setBase(
            "Internal error in TagLocalStorageManagerAsyncTester: "
            "tag in onExpungeTagCompleted slot doesn't "
            "match the original expunged Tag");

        QNWARNING("tests:local_storage", errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    m_state = STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST;
    Q_EMIT findTagRequest(m_foundTag, QUuid::createUuid());
}

void TagLocalStorageManagerAsyncTester::onExpungeTagFailed(
    Tag tag, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId << ", tag: " << tag);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void TagLocalStorageManagerAsyncTester::createConnections()
{
    QObject::connect(
        m_pLocalStorageManagerThread, &QThread::finished,
        m_pLocalStorageManagerThread, &QThread::deleteLater);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::initialized,
        this, &TagLocalStorageManagerAsyncTester::initialize);

    // Request --> slot connections
    QObject::connect(
        this, &TagLocalStorageManagerAsyncTester::getTagCountRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onGetTagCountRequest);

    QObject::connect(
        this, &TagLocalStorageManagerAsyncTester::addTagRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onAddTagRequest);

    QObject::connect(
        this, &TagLocalStorageManagerAsyncTester::updateTagRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateTagRequest);

    QObject::connect(
        this, &TagLocalStorageManagerAsyncTester::findTagRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onFindTagRequest);

    QObject::connect(
        this, &TagLocalStorageManagerAsyncTester::listAllTagsRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onListAllTagsRequest);

    QObject::connect(
        this, &TagLocalStorageManagerAsyncTester::expungeTagRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeTagRequest);

    // Slot <-- result connections
    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::getTagCountComplete, this,
        &TagLocalStorageManagerAsyncTester::onGetTagCountCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::getTagCountFailed, this,
        &TagLocalStorageManagerAsyncTester::onGetTagCountFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::addTagComplete,
        this, &TagLocalStorageManagerAsyncTester::onAddTagCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::addTagFailed,
        this, &TagLocalStorageManagerAsyncTester::onAddTagFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateTagComplete, this,
        &TagLocalStorageManagerAsyncTester::onUpdateTagCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::updateTagFailed,
        this, &TagLocalStorageManagerAsyncTester::onUpdateTagFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::findTagComplete,
        this, &TagLocalStorageManagerAsyncTester::onFindTagCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::findTagFailed,
        this, &TagLocalStorageManagerAsyncTester::onFindTagFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::listAllTagsComplete, this,
        &TagLocalStorageManagerAsyncTester::onListAllTagsCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::listAllTagsFailed, this,
        &TagLocalStorageManagerAsyncTester::onListAllTagsFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::expungeTagComplete, this,
        &TagLocalStorageManagerAsyncTester::onExpungeTagCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::expungeTagFailed, this,
        &TagLocalStorageManagerAsyncTester::onExpungeTagFailed);
}

void TagLocalStorageManagerAsyncTester::clear()
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
