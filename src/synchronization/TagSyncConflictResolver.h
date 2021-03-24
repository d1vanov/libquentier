/*
 * Copyright 2017-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_TAG_SYNC_CONFLICT_RESOLVER_H
#define LIB_QUENTIER_SYNCHRONIZATION_TAG_SYNC_CONFLICT_RESOLVER_H

#include <quentier/types/ErrorString.h>

#include <qevercloud/types/Tag.h>

#include <QObject>
#include <QUuid>

namespace quentier {

class LocalStorageManagerAsync;
class TagSyncCache;

/**
 * The TagSyncConflictResolver class resolves the conflict between two tags:
 * the one downloaded from the remote server and the local one. The conflict
 * resolution might involve changes in other tags, seemingly unrelated
 * to the currently conflicting ones
 */
class Q_DECL_HIDDEN TagSyncConflictResolver final : public QObject
{
    Q_OBJECT
public:
    explicit TagSyncConflictResolver(
        qevercloud::Tag remoteTag, QString remoteTagLinkedNotebookGuid,
        qevercloud::Tag localConflict, TagSyncCache & cache,
        LocalStorageManagerAsync & localStorageManagerAsync,
        QObject * parent = nullptr);

    void start();

    [[nodiscard]] const qevercloud::Tag & remoteTag() const noexcept
    {
        return m_remoteTag;
    }

    [[nodiscard]] const qevercloud::Tag & localConflict() const noexcept
    {
        return m_localConflict;
    }

Q_SIGNALS:
    void finished(qevercloud::Tag remoteTag);
    void failure(qevercloud::Tag remoteTag, ErrorString errorDescription);

    // private signals
    void fillTagsCache();
    void addTag(qevercloud::Tag tag, QUuid requestId);
    void updateTag(qevercloud::Tag tag, QUuid requestId);
    void findTag(qevercloud::Tag tag, QUuid requestId);

private Q_SLOTS:
    void onAddTagComplete(qevercloud::Tag tag, QUuid requestId);

    void onAddTagFailed(
        qevercloud::Tag tag, ErrorString errorDescription, QUuid requestId);

    void onUpdateTagComplete(qevercloud::Tag tag, QUuid requestId);

    void onUpdateTagFailed(
        qevercloud::Tag tag, ErrorString errorDescription, QUuid requestId);

    void onFindTagComplete(qevercloud::Tag tag, QUuid requestId);

    void onFindTagFailed(
        qevercloud::Tag tag, ErrorString errorDescription, QUuid requestId);

    void onCacheFilled();
    void onCacheFailed(ErrorString errorDescription);

private:
    void connectToLocalStorage();
    void processTagsConflictByGuid();
    void processTagsConflictByName(const qevercloud::Tag & localConflict);
    void overrideLocalChangesWithRemoteChanges();
    void renameConflictingLocalTag(const qevercloud::Tag & localConflict);

    enum class State
    {
        Undefined = 0,
        OverrideLocalChangesWithRemoteChanges,
        PendingConflictingTagRenaming,
        PendingRemoteTagAdoptionInLocalStorage
    };

    friend QDebug & operator<<(QDebug & dbg, State state);

private:
    TagSyncCache & m_cache;
    LocalStorageManagerAsync & m_localStorageManagerAsync;

    qevercloud::Tag m_remoteTag;
    qevercloud::Tag m_localConflict;

    QString m_remoteTagLinkedNotebookGuid;

    qevercloud::Tag m_tagToBeRenamed;

    State m_state = State::Undefined;

    QUuid m_addTagRequestId;
    QUuid m_updateTagRequestId;
    QUuid m_findTagRequestId;

    bool m_started = false;
    bool m_pendingCacheFilling = false;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_TAG_SYNC_CONFLICT_RESOLVER_H
