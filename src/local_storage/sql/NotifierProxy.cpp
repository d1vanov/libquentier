/*
 * Copyright 2021-2023 Dmitry Ivanov
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

#include "Notifier.h"
#include "NotifierProxy.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Post.h>

namespace quentier::local_storage::sql {

NotifierProxy::NotifierProxy(threading::QThreadPtr writerThread) :
    m_writerThread{std::move(writerThread)}
{
    if (Q_UNLIKELY(!m_writerThread)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("NotifierProxy: writer thread is null")}};
    }

    m_notifier = new Notifier(m_writerThread.get());
}

NotifierProxy::~NotifierProxy()
{
    m_notifier->deleteLater();
}

ILocalStorageNotifier * NotifierProxy::notifier() const noexcept
{
    return m_notifier;
}

void NotifierProxy::notifyUserPut(qevercloud::User user)
{
    if (QThread::currentThread() == m_writerThread.get()) {
        m_notifier->notifyUserPut(std::move(user));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, user = std::move(user)] () mutable
        {
            notifier->notifyUserPut(std::move(user));
        });
}

void NotifierProxy::notifyUserExpunged(qevercloud::UserID userId)
{
    if (QThread::currentThread() == m_writerThread.get()) {
        m_notifier->notifyUserExpunged(userId);
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, userId] () mutable
        {
            notifier->notifyUserExpunged(userId);
        });
}

void NotifierProxy::notifyNotebookPut(qevercloud::Notebook notebook)
{
    if (QThread::currentThread() == m_writerThread.get()) {
        m_notifier->notifyNotebookPut(std::move(notebook));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, notebook = std::move(notebook)] () mutable
        {
            notifier->notifyNotebookPut(std::move(notebook));
        });
}

void NotifierProxy::notifyNotebookExpunged(QString notebookLocalId)
{
    if (QThread::currentThread() == m_writerThread.get()) {
        m_notifier->notifyNotebookExpunged(std::move(notebookLocalId));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier,
         notebookLocalId = std::move(notebookLocalId)]() mutable {
            notifier->notifyNotebookExpunged(std::move(notebookLocalId));
        });
}

void NotifierProxy::notifyLinkedNotebookPut(
    qevercloud::LinkedNotebook linkedNotebook)
{
    if (QThread::currentThread() == m_writerThread.get()) {
        m_notifier->notifyLinkedNotebookPut(std::move(linkedNotebook));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier,
         linkedNotebook = std::move(linkedNotebook)]() mutable {
            notifier->notifyLinkedNotebookPut(std::move(linkedNotebook));
        });
}

void NotifierProxy::notifyLinkedNotebookExpunged(
    qevercloud::Guid linkedNotebookGuid)
{
    if (QThread::currentThread() == m_writerThread.get()) {
        m_notifier->notifyLinkedNotebookExpunged(std::move(linkedNotebookGuid));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier,
         linkedNotebookGuid = std::move(linkedNotebookGuid)]() mutable {
            notifier->notifyLinkedNotebookExpunged(
                std::move(linkedNotebookGuid));
        });
}

void NotifierProxy::notifyNotePut(qevercloud::Note note)
{
    if (QThread::currentThread() == m_writerThread.get()) {
        m_notifier->notifyNotePut(std::move(note));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, note = std::move(note)] () mutable
        {
            notifier->notifyNotePut(std::move(note));
        });
}

void NotifierProxy::notifyNoteUpdated(
    qevercloud::Note note, ILocalStorage::UpdateNoteOptions options)
{
    if (QThread::currentThread() == m_writerThread.get()) {
        m_notifier->notifyNoteUpdated(std::move(note), options);
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, note = std::move(note), options] () mutable
        {
            notifier->notifyNoteUpdated(std::move(note), options);
        });
}

void NotifierProxy::notifyNoteNotebookChanged(
    QString noteLocalId, QString previousNotebookLocalId,
    QString newNotebookLocalId)
{
    if (QThread::currentThread() == m_writerThread.get()) {
        m_notifier->notifyNoteNotebookChanged(
            std::move(noteLocalId), std::move(previousNotebookLocalId),
            std::move(newNotebookLocalId));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, noteLocalId = std::move(noteLocalId),
         previousNotebookLocalId = std::move(previousNotebookLocalId),
         newNotebookLocalId = std::move(newNotebookLocalId)]() mutable {
            notifier->notifyNoteNotebookChanged(
                std::move(noteLocalId), std::move(previousNotebookLocalId),
                std::move(newNotebookLocalId));
        });
}

void NotifierProxy::notifyNoteTagListChanged(
    QString noteLocalId, QStringList previousNoteTagLocalIds,
    QStringList newNoteTagLocalIds)
{
    if (QThread::currentThread() == m_writerThread.get()) {
        m_notifier->notifyNoteTagListChanged(
            std::move(noteLocalId), std::move(previousNoteTagLocalIds),
            std::move(newNoteTagLocalIds));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, noteLocalId = std::move(noteLocalId),
         previousNoteTagLocalIds = std::move(previousNoteTagLocalIds),
         newNoteTagLocalIds = std::move(newNoteTagLocalIds)] () mutable {
            notifier->notifyNoteTagListChanged(
                std::move(noteLocalId), std::move(previousNoteTagLocalIds),
                std::move(newNoteTagLocalIds));
        });
}

void NotifierProxy::notifyNoteExpunged(QString noteLocalId)
{
    if (QThread::currentThread() == m_writerThread.get()) {
        m_notifier->notifyNoteExpunged(std::move(noteLocalId));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier,
         noteLocalId = std::move(noteLocalId)]() mutable {
            notifier->notifyNoteExpunged(std::move(noteLocalId));
        });
}

void NotifierProxy::notifyTagPut(qevercloud::Tag tag)
{
    if (QThread::currentThread() == m_writerThread.get()) {
        m_notifier->notifyTagPut(std::move(tag));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, tag = std::move(tag)] () mutable
        {
            notifier->notifyTagPut(std::move(tag));
        });
}

void NotifierProxy::notifyTagExpunged(
    QString tagLocalId, QStringList expungedChildTagLocalIds)
{
    if (QThread::currentThread() == m_writerThread.get()) {
        m_notifier->notifyTagExpunged(
            std::move(tagLocalId), std::move(expungedChildTagLocalIds));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, tagLocalId = std::move(tagLocalId),
         expungedChildTagLocalIds =
             std::move(expungedChildTagLocalIds)]() mutable {
            notifier->notifyTagExpunged(
                std::move(tagLocalId), std::move(expungedChildTagLocalIds));
        });
}

void NotifierProxy::notifyResourcePut(qevercloud::Resource resource)
{
    if (QThread::currentThread() == m_writerThread.get()) {
        m_notifier->notifyResourcePut(std::move(resource));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, resource = std::move(resource)] () mutable
        {
            notifier->notifyResourcePut(std::move(resource));
        });
}

void NotifierProxy::notifyResourceExpunged(QString resourceLocalId)
{
    if (QThread::currentThread() == m_writerThread.get()) {
        m_notifier->notifyResourceExpunged(std::move(resourceLocalId));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier,
         resourceLocalId = std::move(resourceLocalId)]() mutable {
            notifier->notifyResourceExpunged(std::move(resourceLocalId));
        });
}

void NotifierProxy::notifySavedSearchPut(qevercloud::SavedSearch savedSearch)
{
    if (QThread::currentThread() == m_writerThread.get()) {
        m_notifier->notifySavedSearchPut(std::move(savedSearch));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, savedSearch = std::move(savedSearch)] () mutable
        {
            notifier->notifySavedSearchPut(std::move(savedSearch));
        });
}

void NotifierProxy::notifySavedSearchExpunged(QString savedSearchLocalId)
{
    if (QThread::currentThread() == m_writerThread.get()) {
        m_notifier->notifySavedSearchExpunged(std::move(savedSearchLocalId));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier,
         savedSearchLocalId = std::move(savedSearchLocalId)]() mutable {
            notifier->notifySavedSearchExpunged(std::move(savedSearchLocalId));
        });
}

} // namespace quentier::local_storage::sql
