/*
 * Copyright 2021-2022 Dmitry Ivanov
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

#pragma once

#include "Fwd.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/threading/Fwd.h>

namespace quentier::local_storage::sql {

/**
 * @brief The NotifierProxy class proxies notifications to the internally
 * created and managed object of Notifier class.
 *
 * The purpose behind the proxy object is to ensure methods of Notifier
 * are always called from the writer thread. It also allows to manage the
 * lifetime of Notifier object more properly: Notifier is guaranteed to be alive
 * at least for as long as NotifierProxy.
 */
class NotifierProxy
{
public:
    explicit NotifierProxy(threading::QThreadPtr writerThread);
    ~NotifierProxy();

    // For subscription to signals
    [[nodiscard]] ILocalStorageNotifier * notifier() const noexcept;

    void notifyUserPut(qevercloud::User user);
    void notifyUserExpunged(qevercloud::UserID userId);

    void notifyNotebookPut(qevercloud::Notebook notebook);
    void notifyNotebookExpunged(QString notebookLocalId);

    void notifyLinkedNotebookPut(qevercloud::LinkedNotebook linkedNotebook);
    void notifyLinkedNotebookExpunged(qevercloud::Guid linkedNotebookGuid);

    void notifyNotePut(qevercloud::Note note);

    void notifyNoteUpdated(
        qevercloud::Note note, ILocalStorage::UpdateNoteOptions options);

    void notifyNoteNotebookChanged(
        QString noteLocalId, QString previousNotebookLocalId,
        QString newNotebookLocalId);

    void notifyNoteTagListChanged(
        QString noteLocalId, QStringList previousNoteTagLocalIds,
        QStringList newNoteTagLocalIds);

    void notifyNoteExpunged(QString noteLocalId);

    void notifyTagPut(qevercloud::Tag tag);

    void notifyTagExpunged(
        QString tagLocalId, QStringList expungedChildTagLocalIds);

    void notifyResourcePut(qevercloud::Resource resource);
    void notifyResourceExpunged(QString resourceLocalId);

    void notifySavedSearchPut(qevercloud::SavedSearch savedSearch);
    void notifySavedSearchExpunged(QString savedSearchLocalId);

private:
    const threading::QThreadPtr m_writerThread;
    Notifier * m_notifier = nullptr;
};

} // namespace quentier::local_storage::sql
