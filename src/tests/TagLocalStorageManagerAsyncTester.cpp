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

#include "TagLocalStorageManagerAsyncTester.h"
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <QThread>

namespace quentier {
namespace test {

TagLocalStorageManagerAsyncTester::TagLocalStorageManagerAsyncTester(QObject * parent) :
    QObject(parent),
    m_state(STATE_UNINITIALIZED),
    m_pLocalStorageManagerAsync(Q_NULLPTR),
    m_pLocalStorageManagerThread(Q_NULLPTR),
    m_initialTag(),
    m_foundTag(),
    m_modifiedTag(),
    m_initialTags()
{}

TagLocalStorageManagerAsyncTester::~TagLocalStorageManagerAsyncTester()
{
    clear();
}

void TagLocalStorageManagerAsyncTester::onInitTestCase()
{
    QString username = QStringLiteral("TagLocalStorageManagerAsyncTester");
    qint32 userId = 2;
    bool startFromScratch = true;
    bool overrideLock = false;

    clear();

    m_pLocalStorageManagerThread = new QThread(this);
    Account account(username, Account::Type::Evernote, userId);
    m_pLocalStorageManagerAsync = new LocalStorageManagerAsync(account, startFromScratch, overrideLock);
    m_pLocalStorageManagerAsync->moveToThread(m_pLocalStorageManagerThread);

    createConnections();

    m_pLocalStorageManagerThread->start();
}

void TagLocalStorageManagerAsyncTester::onWorkerInitialized()
{
    m_initialTag = Tag();
    m_initialTag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    m_initialTag.setUpdateSequenceNumber(3);
    m_initialTag.setName(QStringLiteral("Fake tag name"));

    ErrorString errorDescription;
    if (!m_initialTag.checkParameters(errorDescription)) {
        QNWARNING(QStringLiteral("Found invalid Tag: ") << m_initialTag << QStringLiteral(", error: ") << errorDescription);
        emit failure(errorDescription.nonLocalizedString());
        return;
    }

    m_state = STATE_SENT_ADD_REQUEST;
    emit addTagRequest(m_initialTag);
}

void TagLocalStorageManagerAsyncTester::onGetTagCountCompleted(int count, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

#define HANDLE_WRONG_STATE() \
    else { \
        errorDescription.base() = QStringLiteral("Internal error in TagLocalStorageManagerAsyncTester: found wrong state"); \
        emit failure(errorDescription.nonLocalizedString()); \
        return; \
    }

    if (m_state == STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST)
    {
        if (count != 1) {
            errorDescription.base() = QStringLiteral("GetTagCount returned result different from the expected one (1): ");
            errorDescription.details() = QString::number(count);
            QNWARNING(errorDescription);
            emit failure(errorDescription.nonLocalizedString());
            return;
        }

        m_modifiedTag.setLocal(true);
        m_state = STATE_SENT_EXPUNGE_REQUEST;
        emit expungeTagRequest(m_modifiedTag);
    }
    else if (m_state == STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST)
    {
        if (count != 0) {
            errorDescription.base() = QStringLiteral("GetTagCount returned result different from the expected one (0): ");
            errorDescription.details() = QString::number(count);
            QNWARNING(errorDescription);
            emit failure(errorDescription.nonLocalizedString());
            return;
        }

        Tag extraTag;
        extraTag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000001"));
        extraTag.setUpdateSequenceNumber(1);
        extraTag.setName(QStringLiteral("Extra tag name one"));

        m_state = STATE_SENT_ADD_EXTRA_TAG_ONE_REQUEST;
        emit addTagRequest(extraTag);
    }
    HANDLE_WRONG_STATE();
}

void TagLocalStorageManagerAsyncTester::onGetTagCountFailed(ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << QStringLiteral(", requestId = ") << requestId);
    emit failure(errorDescription.nonLocalizedString());
}

void TagLocalStorageManagerAsyncTester::onAddTagCompleted(Tag tag, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_ADD_REQUEST)
    {
        if (m_initialTag != tag) {
            errorDescription.base() = QStringLiteral("Internal error in TagLocalStorageManagerAsyncTester: "
                                                     "tag in onAddTagCompleted slot doesn't match the original Tag");
            QNWARNING(errorDescription);
            emit failure(errorDescription.nonLocalizedString());
            return;
        }

        m_foundTag = Tag();
        m_foundTag.setLocalUid(tag.localUid());

        m_state = STATE_SENT_FIND_AFTER_ADD_REQUEST;
        emit findTagRequest(m_foundTag);
    }
    else if (m_state == STATE_SENT_ADD_EXTRA_TAG_ONE_REQUEST)
    {
        m_initialTags << tag;

        Tag extraTag;
        extraTag.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000002"));
        extraTag.setUpdateSequenceNumber(2);
        extraTag.setName(QStringLiteral("Extra tag name two"));
        extraTag.setParentGuid(tag.guid());

        m_state = STATE_SENT_ADD_EXTRA_TAG_TWO_REQUEST;
        emit addTagRequest(extraTag);
    }
    else if (m_state == STATE_SENT_ADD_EXTRA_TAG_TWO_REQUEST)
    {
        m_initialTags << tag;

        m_state = STATE_SENT_LIST_TAGS_REQUEST;
        size_t limit = 0, offset = 0;
        LocalStorageManager::ListTagsOrder::type order = LocalStorageManager::ListTagsOrder::NoOrder;
        LocalStorageManager::OrderDirection::type orderDirection = LocalStorageManager::OrderDirection::Ascending;
        QString linkedNotebookGuid;
        emit listAllTagsRequest(limit, offset, order, orderDirection, linkedNotebookGuid);
    }
    HANDLE_WRONG_STATE();
}

void TagLocalStorageManagerAsyncTester::onAddTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << QStringLiteral(", request id = ") << requestId << QStringLiteral(", tag: ") << tag);
    emit failure(errorDescription.nonLocalizedString());
}

void TagLocalStorageManagerAsyncTester::onUpdateTagCompleted(Tag tag, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_UPDATE_REQUEST)
    {
        if (m_modifiedTag != tag) {
            errorDescription.base() = QStringLiteral("Internal error in TagLocalStorageManagerAsyncTester: "
                                                     "tag in onUpdateTagCompleted slot doesn't match "
                                                     "the original modified Tag");
            QNWARNING(errorDescription);
            emit failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_FIND_AFTER_UPDATE_REQUEST;
        emit findTagRequest(m_foundTag);
    }
    HANDLE_WRONG_STATE();
}

void TagLocalStorageManagerAsyncTester::onUpdateTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << QStringLiteral(", requestId = ") << requestId << QStringLiteral(", tag: ") << tag);
    emit failure(errorDescription.nonLocalizedString());
}

void TagLocalStorageManagerAsyncTester::onFindTagCompleted(Tag tag, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_FIND_AFTER_ADD_REQUEST)
    {
        if (tag != m_initialTag) {
            errorDescription.base() = QStringLiteral("Added and found tags in local storage don't match");
            QNWARNING(errorDescription << QStringLiteral(": Tag added to LocalStorageManager: ") << m_initialTag
                      << QStringLiteral("\nTag found in LocalStorageManager: ") << m_foundTag);
            emit failure(errorDescription.nonLocalizedString());
            return;
        }

        // Attempt to find tag by name now
        Tag tagToFindByName;
        tagToFindByName.unsetLocalUid();
        tagToFindByName.setName(m_initialTag.name());

        m_state = STATE_SENT_FIND_BY_NAME_AFTER_ADD_REQUEST;
        emit findTagRequest(tagToFindByName);
    }
    else if (m_state == STATE_SENT_FIND_BY_NAME_AFTER_ADD_REQUEST)
    {
        if (tag != m_initialTag) {
            errorDescription.base() = QStringLiteral("Added and found by name tags in local storage don't match");
            QNWARNING(errorDescription << QStringLiteral(": Tag added to LocalStorageManager: ") << m_initialTag
                      << QStringLiteral("\nTag found in LocalStorageManager: ") << m_foundTag);
            emit failure(errorDescription.nonLocalizedString());
            return;
        }

        // Ok, found tag is good, updating it now
        m_modifiedTag = m_initialTag;
        m_modifiedTag.setUpdateSequenceNumber(m_initialTag.updateSequenceNumber() + 1);
        m_modifiedTag.setName(m_initialTag.name() + QStringLiteral("_modified"));

        m_state = STATE_SENT_UPDATE_REQUEST;
        emit updateTagRequest(m_modifiedTag);
    }
    else if (m_state == STATE_SENT_FIND_AFTER_UPDATE_REQUEST)
    {
        if (tag != m_modifiedTag) {
            errorDescription.base() = QStringLiteral("Updated and found tags in local storage don't match");
            QNWARNING(errorDescription << QStringLiteral(": Tag updated in LocalStorageManager: ") << m_modifiedTag
                      << QStringLiteral("\nTag found in LocalStorageManager: ") << m_foundTag);
            emit failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST;
        emit getTagCountRequest();
    }
    else if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST)
    {
        errorDescription.base() = QStringLiteral("Found tag which should have been expunged from local storage");
        QNWARNING(errorDescription << QStringLiteral(": Tag expunged from LocalStorageManager: ") << m_modifiedTag
                  << QStringLiteral("\nTag found in LocalStorageManager: ") << m_foundTag);
        emit failure(errorDescription.nonLocalizedString());
        return;
    }
    HANDLE_WRONG_STATE();
}

void TagLocalStorageManagerAsyncTester::onFindTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId)
{
    if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST) {
        m_state = STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST;
        emit getTagCountRequest();
        return;
    }

    QNWARNING(errorDescription << QStringLiteral(", requestId = ") << requestId << QStringLiteral(", tag: ") << tag);
    emit failure(errorDescription.nonLocalizedString());
}

void TagLocalStorageManagerAsyncTester::onListAllTagsCompleted(size_t limit, size_t offset,
                                                               LocalStorageManager::ListTagsOrder::type order,
                                                               LocalStorageManager::OrderDirection::type orderDirection,
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
        errorDescription.base() = QStringLiteral("Error: number of found tags does not correspond "
                                                 "to the number of original added tags");
        QNWARNING(errorDescription);
        emit failure(errorDescription.nonLocalizedString());
        return;
    }

    foreach(const Tag & tag, m_initialTags)
    {
        if (!tags.contains(tag)) {
            errorDescription.base() = QStringLiteral("One of initial tags was not found within found tags");
            QNWARNING(errorDescription);
            emit failure(errorDescription.nonLocalizedString());
            return;
        }
    }

    emit success();
}

void TagLocalStorageManagerAsyncTester::onListAllTagsFailed(size_t limit, size_t offset,
                                                            LocalStorageManager::ListTagsOrder::type order,
                                                            LocalStorageManager::OrderDirection::type orderDirection,
                                                            QString linkedNotebookGuid, ErrorString errorDescription, QUuid requestId)
{
    Q_UNUSED(limit)
    Q_UNUSED(offset)
    Q_UNUSED(order)
    Q_UNUSED(orderDirection)
    Q_UNUSED(linkedNotebookGuid)

    QNWARNING(errorDescription << QStringLiteral(", requestId = ") << requestId);
    emit failure(errorDescription.nonLocalizedString());
}

void TagLocalStorageManagerAsyncTester::onExpungeTagCompleted(Tag tag, QStringList expungedChildTagLocalUids, QUuid requestId)
{
    Q_UNUSED(requestId)
    Q_UNUSED(expungedChildTagLocalUids)

    ErrorString errorDescription;

    if (m_modifiedTag != tag) {
        errorDescription.base() = QStringLiteral("Internal error in TagLocalStorageManagerAsyncTester: "
                                                 "tag in onExpungeTagCompleted slot doesn't match "
                                                 "the original expunged Tag");
        QNWARNING(errorDescription);
        emit failure(errorDescription.nonLocalizedString());
        return;
    }

    m_state = STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST;
    emit findTagRequest(m_foundTag);
}

void TagLocalStorageManagerAsyncTester::onExpungeTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << QStringLiteral(", requestId = ") << requestId << QStringLiteral(", tag: ") << tag);
    emit failure(errorDescription.nonLocalizedString());
}

void TagLocalStorageManagerAsyncTester::onFailure(ErrorString errorDescription)
{
    QNWARNING(QStringLiteral("TagLocalStorageManagerAsyncTester::onFailure: ") << errorDescription);
    emit failure(errorDescription.nonLocalizedString());
}

void TagLocalStorageManagerAsyncTester::createConnections()
{
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,failure,ErrorString),
                     this, QNSLOT(TagLocalStorageManagerAsyncTester,onFailure,ErrorString));

    QObject::connect(m_pLocalStorageManagerThread, QNSIGNAL(QThread,started),
                     m_pLocalStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,init));
    QObject::connect(m_pLocalStorageManagerThread, QNSIGNAL(QThread,finished),
                     m_pLocalStorageManagerThread, QNSLOT(QThread,deleteLater));

    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,initialized),
                     this, QNSLOT(TagLocalStorageManagerAsyncTester,onWorkerInitialized));

    // Request --> slot connections
    QObject::connect(this, QNSIGNAL(TagLocalStorageManagerAsyncTester,getTagCountRequest,QUuid),
                     m_pLocalStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onGetTagCountRequest,QUuid));
    QObject::connect(this, QNSIGNAL(TagLocalStorageManagerAsyncTester,addTagRequest,Tag,QUuid),
                     m_pLocalStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddTagRequest,Tag,QUuid));
    QObject::connect(this, QNSIGNAL(TagLocalStorageManagerAsyncTester,updateTagRequest,Tag,QUuid),
                     m_pLocalStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateTagRequest,Tag,QUuid));
    QObject::connect(this, QNSIGNAL(TagLocalStorageManagerAsyncTester,findTagRequest,Tag,QUuid),
                     m_pLocalStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindTagRequest,Tag,QUuid));
    QObject::connect(this,
                     QNSIGNAL(TagLocalStorageManagerAsyncTester,listAllTagsRequest,size_t,size_t,
                              LocalStorageManager::ListTagsOrder::type,LocalStorageManager::OrderDirection::type,QString,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onListAllTagsRequest,size_t,size_t,
                            LocalStorageManager::ListTagsOrder::type,LocalStorageManager::OrderDirection::type,QString,QUuid));
    QObject::connect(this, QNSIGNAL(TagLocalStorageManagerAsyncTester,expungeTagRequest,Tag,QUuid),
                     m_pLocalStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeTagRequest,Tag,QUuid));

    // Slot <-- result connections
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,getTagCountComplete,int,QUuid),
                     this, QNSLOT(TagLocalStorageManagerAsyncTester,onGetTagCountCompleted,int,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,getTagCountFailed,ErrorString,QUuid),
                     this, QNSLOT(TagLocalStorageManagerAsyncTester,onGetTagCountFailed,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagComplete,Tag,QUuid),
                     this, QNSLOT(TagLocalStorageManagerAsyncTester,onAddTagCompleted,Tag,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagFailed,Tag,ErrorString,QUuid),
                     this, QNSLOT(TagLocalStorageManagerAsyncTester,onAddTagFailed,Tag,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagComplete,Tag,QUuid),
                     this, QNSLOT(TagLocalStorageManagerAsyncTester,onUpdateTagCompleted,Tag,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagFailed,Tag,ErrorString,QUuid),
                     this, QNSLOT(TagLocalStorageManagerAsyncTester,onUpdateTagFailed,Tag,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findTagComplete,Tag,QUuid),
                     this, QNSLOT(TagLocalStorageManagerAsyncTester,onFindTagCompleted,Tag,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findTagFailed,Tag,ErrorString,QUuid),
                     this, QNSLOT(TagLocalStorageManagerAsyncTester,onFindTagFailed,Tag,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listAllTagsComplete,size_t,size_t,LocalStorageManager::ListTagsOrder::type,
                              LocalStorageManager::OrderDirection::type,QString,QList<Tag>,QUuid),
                     this,
                     QNSLOT(TagLocalStorageManagerAsyncTester,onListAllTagsCompleted,size_t,size_t,LocalStorageManager::ListTagsOrder::type,
                            LocalStorageManager::OrderDirection::type,QString,QList<Tag>,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listAllTagsFailed,size_t,size_t,LocalStorageManager::ListTagsOrder::type,
                              LocalStorageManager::OrderDirection::type,QString,ErrorString,QUuid),
                     this,
                     QNSLOT(TagLocalStorageManagerAsyncTester,onListAllTagsFailed,size_t,size_t,LocalStorageManager::ListTagsOrder::type,
                            LocalStorageManager::OrderDirection::type,QString,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeTagComplete,Tag,QStringList,QUuid),
                     this, QNSLOT(TagLocalStorageManagerAsyncTester,onExpungeTagCompleted,Tag,QStringList,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeTagFailed,Tag,ErrorString,QUuid),
                     this, QNSLOT(TagLocalStorageManagerAsyncTester,onExpungeTagFailed,Tag,ErrorString,QUuid));
}

void TagLocalStorageManagerAsyncTester::clear()
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
