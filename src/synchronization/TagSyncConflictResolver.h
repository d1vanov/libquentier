/*
 * Copyright 2017 Dmitry Ivanov
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

#include <quentier/types/Tag.h>
#include <QObject>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5qevercloud/QEverCloud.h>
#else
#include <qt4qevercloud/QEverCloud.h>
#endif

namespace quentier {

QT_FORWARD_DECLARE_CLASS(TagSyncCache)
QT_FORWARD_DECLARE_CLASS(LocalStorageManagerAsync)

/**
 * The TagSyncConflictResolver class resolver resolves the conflict between two tags: the one downloaded
 * from the remote server and the local one. The conflict resolution might involve
 * changes in other tags, seemingly unrelated to the currently conflicting ones
 */
class TagSyncConflictResolver: public QObject
{
    Q_OBJECT
public:
    explicit TagSyncConflictResolver(const qevercloud::Tag & remoteTag,
                                     const Tag & localConflict,
                                     TagSyncCache & cache,
                                     LocalStorageManagerAsync & localStorageManagerAsync,
                                     QObject * parent = Q_NULLPTR);

    void start();

    const qevercloud::Tag & remoteTag() const { return m_remoteTag; }
    const Tag & localConflict() const { return m_localConflict; }

Q_SIGNALS:
    void finished(qevercloud::Tag remoteTag);
    void failure(qevercloud::Tag remoteTag, ErrorString errorDescription);

// private signals
    void fillTagsCache();
    void addTag(Tag tag, QUuid requestId);
    void updateTag(Tag tag, QUuid requestId);
    void findTag(Tag tag, QUuid requestId);

private Q_SLOTS:
    void onAddTagComplete(Tag tag, QUuid requestId);
    void onAddTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId);
    void onUpdateTagComplete(Tag tag, QUuid requestId);
    void onUpdateTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId);
    void onFindTagComplete(Tag tag, QUuid requestId);
    void onFindTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId);

    void onCacheFilled();
    void onCacheFailed(ErrorString errorDescription);

private:
    void connectToLocalStorage();
    void processTagsConflictByGuid();
    void processTagsConflictByName(const Tag & localConflict);
    void overrideLocalChangesWithRemoteChanges();
    void renameConflictingLocalTag(const Tag & localConflict);

    struct State
    {
        enum type
        {
            Undefined = 0,
            OverrideLocalChangesWithRemoteChanges,
            PendingConflictingTagRenaming,
            PendingRemoteTagAdoptionInLocalStorage
        };
    };

private:
    TagSyncCache &              m_cache;
    LocalStorageManagerAsync &  m_localStorageManagerAsync;

    qevercloud::Tag             m_remoteTag;
    Tag                         m_localConflict;

    Tag                         m_tagToBeRenamed;

    State::type                 m_state;

    QUuid                       m_addTagRequestId;
    QUuid                       m_updateTagRequestId;
    QUuid                       m_findTagRequestId;

    bool                        m_started;
    bool                        m_pendingCacheFilling;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_TAG_SYNC_CONFLICT_RESOLVER_H
