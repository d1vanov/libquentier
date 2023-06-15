/*
 * Copyright 2023 Dmitry Ivanov
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

#include <quentier/synchronization/types/Errors.h>

#include <qevercloud/Constants.h>
#include <qevercloud/Fwd.h>
#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Resource.h>
#include <qevercloud/types/SavedSearch.h>
#include <qevercloud/types/SyncState.h>
#include <qevercloud/types/Tag.h>
#include <qevercloud/types/TypeAliases.h>

#include <QHash>
#include <QNetworkCookie>
#include <QObject>

#include <boost/bimap.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

#include <exception>
#include <optional>
#include <utility>
#include <variant>

QT_BEGIN_NAMESPACE

class QTcpServer;
class QTcpSocket;

QT_END_NAMESPACE

namespace quentier::synchronization::tests {

class NoteStoreServer : public QObject
{
    Q_OBJECT
public:
    struct ItemData
    {
        // Contains automatically generated or adjusted name of the item (to
        // ensure their uniqueness within the account for the items of the
        // corresponding type) if generation and/or adjustment was necessary.
        std::optional<QString> name;

        // Update sequence number assigned to the item
        qint32 usn = 0;
    };

    enum class StopSynchronizationErrorTrigger
    {
        OnGetUserOwnSyncState,
        OnGetLinkedNotebookSyncState,
        OnGetUserOwnSyncChunk,
        OnGetNoteAfterDownloadingUserOwnSyncChunks,
        OnGetResourceAfterDownloadingUserOwnSyncChunks,
        OnGetLinkedNotebookSyncChunk,
        OnGetNoteAfterDownloadingLinkedNotebookSyncChunks,
        OnGetResourceAfterDownloadingLinkedNotebookSyncChunks,
        OnCreateSavedSearch,
        OnUpdateSavedSearch,
        OnCreateTag,
        OnUpdateTag,
        OnCreateNotebook,
        OnUpdateNotebook,
        OnCreateNote,
        OnUpdateNote,
        OnAuthenticateToSharedNotebook
    };

public:
    NoteStoreServer(
        QString authenticationToken, QList<QNetworkCookie> cookies,
        QHash<qevercloud::Guid, QString> linkedNotebookAuthTokensByGuid,
        QObject * parent = nullptr);

    ~NoteStoreServer() override;

    // Saved searches
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::SavedSearch>
        savedSearches() const;

    [[nodiscard]] ItemData putSavedSearch(qevercloud::SavedSearch search);
    [[nodiscard]] std::optional<qevercloud::SavedSearch> findSavedSearch(
        const QString & guid) const;

    void removeSavedSearch(const QString & guid);

    // Expunged saved searches
    void putExpungedSavedSearchGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedSavedSearchGuid(
        const QString & guid) const;

    void removeExpungedSavedSearchGuid(const QString & guid);

    // Tags
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::Tag> tags() const;
    [[nodiscard]] ItemData putTag(qevercloud::Tag & tag);

    [[nodiscard]] std::optional<qevercloud::Tag> findTag(
        const QString & guid) const;

    void removeTag(const QString & guid);

    // Expunged tags
    void putExpungedTagGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedTagGuid(const QString & guid) const;
    void removeExpungedTagGuid(const QString & guid);

    // Notebooks
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::Notebook> notebooks()
        const;

    [[nodiscard]] ItemData putNotebook(qevercloud::Notebook notebook);
    [[nodiscard]] std::optional<qevercloud::Notebook> findNotebook(
        const QString & guid) const;

    void removeNotebook(const QString & guid);

    [[nodiscard]] QList<qevercloud::Notebook>
        findNotebooksForLinkedNotebookGuid(
            const QString & linkedNotebookGuid) const;

    // Expunged notebooks
    void putExpungedNotebookGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedNotebookGuid(const QString & guid) const;
    void removeExpungedNotebookGuid(const QString & guid);

    // Notes
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::Note> notes() const;
    [[nodiscard]] ItemData putNote(qevercloud::Note note);
    [[nodiscard]] std::optional<qevercloud::Note> findNote(
        const QString & guid) const;

    void removeNote(const QString & guid);

    // Expunged notes
    void putExpungedNoteGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedNoteGuid(const QString & guid) const;
    void removeExpungedNoteGuid(const QString & guid);

    [[nodiscard]] QList<qevercloud::Note> getNotesByConflictSourceNoteGuid(
        const QString & conflictSourceNoteGuid) const;

    // Resources
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::Resource> resources()
        const;
    [[nodiscard]] bool putResource(qevercloud::Resource resource);
    [[nodiscard]] std::optional<qevercloud::Resource> findResource(
        const QString & guid) const;

    void removeResource(const QString & guid);

    // Linked notebooks
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::LinkedNotebook>
        linkedNotebooks() const;

    [[nodiscard]] ItemData putLinkedNotebook(
        qevercloud::LinkedNotebook & linkedNotebook);

    [[nodiscard]] std::optional<qevercloud::LinkedNotebook> findLinkedNotebook(
        const QString & guid) const;

    void removeLinkedNotebook(const QString & guid);

    // Expunged linked notebooks
    void putExpungedLinkedNotebookGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedLinkedNotebookGuid(
        const QString & guid) const;

    void removeExpungedLinkedNotebookGuid(const QString & guid);

    // User own sync state
    [[nodiscard]] std::optional<qevercloud::SyncState> userOwnSyncState() const;
    void putUserOwnSyncState(qevercloud::SyncState syncState);
    void removeUserOwnSyncState();

    // Linked notebook sync states
    [[nodiscard]] QHash<qevercloud::Guid, qevercloud::SyncState>
        linkedNotebookSyncStates() const;

    void putLinkedNotebookSyncState(
        const qevercloud::Guid & linkedNotebookGuid,
        qevercloud::SyncState syncState);

    [[nodiscard]] std::optional<qevercloud::SyncState>
        findLinkedNotebookSyncState(
            const qevercloud::Guid & linkedNotebookGuid);

    void removeLinkedNotebookSyncState(
        const qevercloud::Guid & linkedNotebookGuid);

    void clearLinkedNotebookSyncStates();

    // Update sequence numbers
    [[nodiscard]] qint32 currentUserOwnMaxUsn() const;

    [[nodiscard]] std::optional<qint32> currentLinkedNotebookMaxUsn(
        const qevercloud::Guid & linkedNotebookGuid) const;

    // Stop synchronization error
    [[nodiscard]] std::optional<
        std::pair<StopSynchronizationErrorTrigger, StopSynchronizationError>>
        stopSynchronizationError() const;

    void setStopSynchronizationError(
        StopSynchronizationErrorTrigger trigger,
        StopSynchronizationError error);

    void clearStopSynchronizationError();

    // Other
    [[nodiscard]] quint32 maxNumSavedSearches() const noexcept;
    void setMaxNumSavedSearches(quint32 maxNumSavedSearches) noexcept;

    [[nodiscard]] quint32 maxNumTags() const noexcept;
    void setMaxNumTags(quint32 maxNumTags) noexcept;

    [[nodiscard]] quint32 maxNumNotebooks() const noexcept;
    void setMaxNumNotebooks(quint32 maxNumNotebooks) noexcept;

    [[nodiscard]] quint32 maxNumNotes() const noexcept;
    void setMaxNumNotes(quint32 maxNumNotes) noexcept;

    [[nodiscard]] quint64 maxNoteSize() const noexcept;
    void setMaxNoteSize(quint64 maxNoteSize) noexcept;

    [[nodiscard]] quint32 maxNumResourcesPerNote() const noexcept;
    void setMaxNumResourcesPerNote(quint32 maxNumResourcesPerNote) noexcept;

    [[nodiscard]] quint32 maxNumTagsPerNote() const noexcept;
    void setMaxNumTagsPerNote(quint32 maxNumTagsPerNote) noexcept;

    [[nodiscard]] quint64 maxResourceSize() const noexcept;
    void setMaxResourceSize(quint64 maxResourceSize) noexcept;

private Q_SLOTS:
    void onRequestReady(const QByteArray & responseData);

private:
    [[nodiscard]] std::exception_ptr checkAuthentication(
        const qevercloud::IRequestContextPtr & ctx) const;

    [[nodiscard]] std::exception_ptr checkLinkedNotetookAuthentication(
        const qevercloud::IRequestContextPtr & ctx) const;

private:
    const QString m_authenticationToken;
    const QList<QNetworkCookie> m_cookies;
    const QHash<qevercloud::Guid, QString> m_linkedNotebookAuthTokensByGuid;

    QTcpServer * m_tcpServer = nullptr;
    QTcpSocket * m_tcpSocket = nullptr;
    qevercloud::NoteStoreServer * m_server = nullptr;

    // Saved searches
    struct SavedSearchByGuid
    {};

    struct SavedSearchByUSN
    {};

    struct SavedSearchByNameUpper
    {};

    struct SavedSearchDataExtractor
    {
        [[nodiscard]] static QString name(
            const qevercloud::SavedSearch & search)
        {
            return search.name().value_or(QString{});
        }

        [[nodiscard]] static QString nameUpper(
            const qevercloud::SavedSearch & search)
        {
            return name(search).toUpper();
        }

        [[nodiscard]] static qevercloud::Guid guid(
            const qevercloud::SavedSearch & search)
        {
            return search.guid().value_or(qevercloud::Guid{});
        }

        [[nodiscard]] static qint32 updateSequenceNumber(
            const qevercloud::SavedSearch & search)
        {
            return search.updateSequenceNum().value_or(0);
        }
    };

    using SavedSearchData = boost::multi_index_container<
        qevercloud::SavedSearch,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<SavedSearchByGuid>,
                boost::multi_index::global_fun<
                    const qevercloud::SavedSearch &, QString,
                    &SavedSearchDataExtractor::guid>>,
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<SavedSearchByUSN>,
                boost::multi_index::global_fun<
                    const qevercloud::SavedSearch &, qint32,
                    &SavedSearchDataExtractor::updateSequenceNumber>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<SavedSearchByNameUpper>,
                boost::multi_index::global_fun<
                    const qevercloud::SavedSearch &, QString,
                    &SavedSearchDataExtractor::nameUpper>>>>;

    using SavedSearchDataByGuid =
        SavedSearchData::index<SavedSearchByGuid>::type;

    using SavedSearchDataByUSN = SavedSearchData::index<SavedSearchByUSN>::type;

    using SavedSearchDataByNameUpper =
        SavedSearchData::index<SavedSearchByNameUpper>::type;

    // Tags
    struct TagByGuid
    {};

    struct TagByUSN
    {};

    struct TagByNameUpper
    {};

    struct TagByParentTagGuid
    {};

    struct TagByLinkedNotebookGuid
    {};

    struct TagDataExtractor
    {
        [[nodiscard]] static QString name(const qevercloud::Tag & tag)
        {
            return tag.name().value_or(QString{});
        }

        [[nodiscard]] static QString nameUpper(const qevercloud::Tag & tag)
        {
            return name(tag).toUpper();
        }

        [[nodiscard]] static qevercloud::Guid guid(const qevercloud::Tag & tag)
        {
            return tag.guid().value_or(qevercloud::Guid{});
        }

        [[nodiscard]] static qint32 updateSequenceNumber(
            const qevercloud::Tag & tag)
        {
            return tag.updateSequenceNum().value_or(0);
        }

        [[nodiscard]] static qevercloud::Guid parentTagGuid(
            const qevercloud::Tag & tag)
        {
            return tag.parentGuid().value_or(qevercloud::Guid{});
        }

        [[nodiscard]] static qevercloud::Guid linkedNotebookGuid(
            const qevercloud::Tag & tag)
        {
            return tag.linkedNotebookGuid().value_or(qevercloud::Guid{});
        }
    };

    using TagData = boost::multi_index_container<
        qevercloud::Tag,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<TagByGuid>,
                boost::multi_index::global_fun<
                    const qevercloud::Tag &, QString, &TagDataExtractor::guid>>,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<TagByUSN>,
                boost::multi_index::global_fun<
                    const qevercloud::Tag &, qint32,
                    &TagDataExtractor::updateSequenceNumber>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<TagByNameUpper>,
                boost::multi_index::global_fun<
                    const qevercloud::Tag &, QString,
                    &TagDataExtractor::nameUpper>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<TagByParentTagGuid>,
                boost::multi_index::global_fun<
                    const qevercloud::Tag &, QString,
                    &TagDataExtractor::parentTagGuid>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<TagByLinkedNotebookGuid>,
                boost::multi_index::global_fun<
                    const qevercloud::Tag &, QString,
                    &TagDataExtractor::linkedNotebookGuid>>>>;

    using TagDataByGuid = TagData::index<TagByGuid>::type;
    using TagDataByUSN = TagData::index<TagByUSN>::type;
    using TagDataByNameUpper = TagData::index<TagByNameUpper>::type;
    using TagDataByParentTagGuid = TagData::index<TagByParentTagGuid>::type;

    using TagDataByLinkedNotebookGuid =
        TagData::index<TagByLinkedNotebookGuid>::type;

    // Notebooks
    struct NotebookByGuid
    {};

    struct NotebookByUSN
    {};

    struct NotebookByNameUpper
    {};

    struct NotebookByLinkedNotebookGuid
    {};

    struct NotebookDataExtractor
    {
        [[nodiscard]] static QString name(const qevercloud::Notebook & notebook)
        {
            return notebook.name().value_or(QString{});
        }

        [[nodiscard]] static QString nameUpper(
            const qevercloud::Notebook & notebook)
        {
            return name(notebook).toUpper();
        }

        [[nodiscard]] static qevercloud::Guid guid(
            const qevercloud::Notebook & notebook)
        {
            return notebook.guid().value_or(qevercloud::Guid{});
        }

        [[nodiscard]] static qint32 updateSequenceNumber(
            const qevercloud::Notebook & notebook)
        {
            return notebook.updateSequenceNum().value_or(0);
        }

        [[nodiscard]] static qevercloud::Guid linkedNotebookGuid(
            const qevercloud::Notebook & notebook)
        {
            return notebook.linkedNotebookGuid().value_or(qevercloud::Guid{});
        }
    };

    using NotebookData = boost::multi_index_container<
        qevercloud::Notebook,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<NotebookByGuid>,
                boost::multi_index::global_fun<
                    const qevercloud::Notebook &, QString,
                    &NotebookDataExtractor::guid>>,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<NotebookByUSN>,
                boost::multi_index::global_fun<
                    const qevercloud::Notebook &, qint32,
                    &NotebookDataExtractor::updateSequenceNumber>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<NotebookByNameUpper>,
                boost::multi_index::global_fun<
                    const qevercloud::Notebook &, QString,
                    &NotebookDataExtractor::nameUpper>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<NotebookByLinkedNotebookGuid>,
                boost::multi_index::global_fun<
                    const qevercloud::Notebook &, QString,
                    &NotebookDataExtractor::linkedNotebookGuid>>>>;

    using NotebookDataByGuid = NotebookData::index<NotebookByGuid>::type;
    using NotebookDataByUSN = NotebookData::index<NotebookByUSN>::type;

    using NotebookDataByNameUpper =
        NotebookData::index<NotebookByNameUpper>::type;

    using NotebookDataByLinkedNotebookGuid =
        NotebookData::index<NotebookByLinkedNotebookGuid>::type;

    // Notes
    struct NoteByGuid
    {};

    struct NoteByUSN
    {};

    struct NoteByNotebookGuid
    {};

    struct NoteByConflictSourceNoteGuid
    {};

    struct NoteDataExtractor
    {
        [[nodiscard]] static qevercloud::Guid guid(
            const qevercloud::Note & note)
        {
            return note.guid().value_or(qevercloud::Guid{});
        }

        [[nodiscard]] static qint32 updateSequenceNumber(
            const qevercloud::Note & note)
        {
            return note.updateSequenceNum().value_or(0);
        }

        [[nodiscard]] static qevercloud::Guid notebookGuid(
            const qevercloud::Note & note)
        {
            return note.notebookGuid().value_or(qevercloud::Guid{});
        }

        [[nodiscard]] static qevercloud::Guid conflictSourceNoteGuid(
            const qevercloud::Note & note)
        {
            if (!note.attributes()) {
                return {};
            }

            const auto & noteAttributes = *note.attributes();
            if (!noteAttributes.conflictSourceNoteGuid()) {
                return {};
            }

            return *noteAttributes.conflictSourceNoteGuid();
        }
    };

    using NoteData = boost::multi_index_container<
        qevercloud::Note,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<NoteByGuid>,
                boost::multi_index::global_fun<
                    const qevercloud::Note &, QString,
                    &NoteDataExtractor::guid>>,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<NoteByUSN>,
                boost::multi_index::global_fun<
                    const qevercloud::Note &, qint32,
                    &NoteDataExtractor::updateSequenceNumber>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<NoteByNotebookGuid>,
                boost::multi_index::global_fun<
                    const qevercloud::Note &, QString,
                    &NoteDataExtractor::notebookGuid>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<NoteByConflictSourceNoteGuid>,
                boost::multi_index::global_fun<
                    const qevercloud::Note &, QString,
                    &NoteDataExtractor::conflictSourceNoteGuid>>>>;

    using NoteDataByGuid = NoteData::index<NoteByGuid>::type;
    using NoteDataByUSN = NoteData::index<NoteByUSN>::type;
    using NoteDataByNotebookGuid = NoteData::index<NoteByNotebookGuid>::type;

    using NoteDataByConflictSourceNoteGuid =
        NoteData::index<NoteByConflictSourceNoteGuid>::type;

    // Resources
    struct ResourceByGuid
    {};

    struct ResourceByUSN
    {};

    struct ResourceByNoteGuid
    {};

    struct ResourceDataExtractor
    {
        [[nodiscard]] static qevercloud::Guid guid(
            const qevercloud::Resource & resource)
        {
            return resource.guid().value_or(qevercloud::Guid{});
        }

        [[nodiscard]] static qint32 updateSequenceNumber(
            const qevercloud::Resource & resource)
        {
            return resource.updateSequenceNum().value_or(0);
        }

        [[nodiscard]] static qevercloud::Guid noteGuid(
            const qevercloud::Resource & resource)
        {
            return resource.noteGuid().value_or(qevercloud::Guid{});
        }
    };

    using ResourceData = boost::multi_index_container<
        qevercloud::Resource,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<ResourceByGuid>,
                boost::multi_index::global_fun<
                    const qevercloud::Resource &, QString,
                    &ResourceDataExtractor::guid>>,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<ResourceByUSN>,
                boost::multi_index::global_fun<
                    const qevercloud::Resource &, qint32,
                    &ResourceDataExtractor::updateSequenceNumber>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<ResourceByNoteGuid>,
                boost::multi_index::global_fun<
                    const qevercloud::Resource &, QString,
                    &ResourceDataExtractor::noteGuid>>>>;

    using ResourceDataByGuid = ResourceData::index<ResourceByGuid>::type;
    using ResourceDataByUSN = ResourceData::index<ResourceByUSN>::type;

    using ResourceDataByNoteGuid =
        ResourceData::index<ResourceByNoteGuid>::type;

    // Linked notebooks
    struct LinkedNotebookByGuid
    {};

    struct LinkedNotebookByUSN
    {};

    struct LinkedNotebookByShardId
    {};

    struct LinkedNotebookByUri
    {};

    struct LinkedNotebookByUsername
    {};

    struct LinkedNotebookBySharedNotebookGlobalId
    {};

    struct LinkedNotebookDataExtractor
    {
        [[nodiscard]] static qevercloud::Guid guid(
            const qevercloud::LinkedNotebook & linkedNotebook)
        {
            return linkedNotebook.guid().value_or(qevercloud::Guid{});
        }

        [[nodiscard]] static QString shardId(
            const qevercloud::LinkedNotebook & linkedNotebook)
        {
            return linkedNotebook.shardId().value_or(QString{});
        }

        [[nodiscard]] static QString uri(
            const qevercloud::LinkedNotebook & linkedNotebook)
        {
            return linkedNotebook.uri().value_or(QString{});
        }

        [[nodiscard]] static QString username(
            const qevercloud::LinkedNotebook & linkedNotebook)
        {
            return linkedNotebook.username().value_or(QString{});
        }

        [[nodiscard]] static qint32 updateSequenceNumber(
            const qevercloud::LinkedNotebook & linkedNotebook)
        {
            return linkedNotebook.updateSequenceNum().value_or(0);
        }

        [[nodiscard]] static QString sharedNotebookGlobalId(
            const qevercloud::LinkedNotebook & linkedNotebook)
        {
            return linkedNotebook.sharedNotebookGlobalId().value_or(QString{});
        }
    };

    using LinkedNotebookData = boost::multi_index_container<
        qevercloud::LinkedNotebook,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<LinkedNotebookByGuid>,
                boost::multi_index::global_fun<
                    const qevercloud::LinkedNotebook &, QString,
                    &LinkedNotebookDataExtractor::guid>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<LinkedNotebookByShardId>,
                boost::multi_index::global_fun<
                    const qevercloud::LinkedNotebook &, QString,
                    &LinkedNotebookDataExtractor::shardId>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<LinkedNotebookByUri>,
                boost::multi_index::global_fun<
                    const qevercloud::LinkedNotebook &, QString,
                    &LinkedNotebookDataExtractor::uri>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<LinkedNotebookByUsername>,
                boost::multi_index::global_fun<
                    const qevercloud::LinkedNotebook &, QString,
                    &LinkedNotebookDataExtractor::username>>,
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<LinkedNotebookByUSN>,
                boost::multi_index::global_fun<
                    const qevercloud::LinkedNotebook &, qint32,
                    &LinkedNotebookDataExtractor::updateSequenceNumber>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<LinkedNotebookBySharedNotebookGlobalId>,
                boost::multi_index::global_fun<
                    const qevercloud::LinkedNotebook &, QString,
                    &LinkedNotebookDataExtractor::sharedNotebookGlobalId>>>>;

    using LinkedNotebookDataByGuid =
        LinkedNotebookData::index<LinkedNotebookByGuid>::type;

    using LinkedNotebookDataByUSN =
        LinkedNotebookData::index<LinkedNotebookByUSN>::type;

    using LinkedNotebookDataByShardId =
        LinkedNotebookData::index<LinkedNotebookByShardId>::type;

    using LinkedNotebookDataByUri =
        LinkedNotebookData::index<LinkedNotebookByUri>::type;

    using LinkedNotebookDataByUsername =
        LinkedNotebookData::index<LinkedNotebookByUsername>::type;

    using LinkedNotebookDataBySharedNotebookGlobalId =
        LinkedNotebookData::index<LinkedNotebookBySharedNotebookGlobalId>::type;
};

} // namespace quentier::synchronization::tests
