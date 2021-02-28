/*
 * Copyright 2018-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_NOTE_STORE_H
#define LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_NOTE_STORE_H

#include <quentier/synchronization/INoteStore.h>

#include <quentier/types/LinkedNotebook.h>
#include <quentier/types/Note.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/Resource.h>
#include <quentier/types/SavedSearch.h>
#include <quentier/types/Tag.h>
#include <quentier/utility/Compat.h>

#include <boost/bimap.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

#include <QHash>
#include <QQueue>
#include <QSet>

#include <memory>

namespace quentier {

class FakeNoteStore : public INoteStore
{
    Q_OBJECT
public:
    explicit FakeNoteStore(QObject * parent = nullptr);

    // Saved searches
    QHash<QString, qevercloud::SavedSearch> savedSearches() const;

    bool setSavedSearch(SavedSearch & search, ErrorString & errorDescription);
    const SavedSearch * findSavedSearch(const QString & guid) const;
    bool removeSavedSearch(const QString & guid);

    void setExpungedSavedSearchGuid(const QString & guid);
    bool containsExpungedSavedSearchGuid(const QString & guid) const;
    bool removeExpungedSavedSearchGuid(const QString & guid);

    // Tags
    QHash<QString, qevercloud::Tag> tags() const;

    bool setTag(Tag & tag, ErrorString & errorDescription);
    const Tag * findTag(const QString & guid) const;
    bool removeTag(const QString & guid);

    void setExpungedTagGuid(const QString & guid);
    bool containsExpungedTagGuid(const QString & guid) const;
    bool removeExpungedTagGuid(const QString & guid);

    // Notebooks
    QHash<QString, qevercloud::Notebook> notebooks() const;

    bool setNotebook(Notebook & notebook, ErrorString & errorDescription);
    const Notebook * findNotebook(const QString & guid) const;
    bool removeNotebook(const QString & guid);

    QList<const Notebook *> findNotebooksForLinkedNotebookGuid(
        const QString & linkedNotebookGuid) const;

    void setExpungedNotebookGuid(const QString & guid);
    bool containsExpungedNotebookGuid(const QString & guid) const;
    bool removeExpungedNotebookGuid(const QString & guid);

    // Notes
    QHash<QString, qevercloud::Note> notes() const;

    bool setNote(Note & note, ErrorString & errorDescription);
    const Note * findNote(const QString & guid) const;
    bool removeNote(const QString & guid);

    void setExpungedNoteGuid(const QString & guid);
    bool containsExpungedNoteGuid(const QString & guid) const;
    bool removeExpungedNoteGuid(const QString & guid);

    QList<Note> getNotesByConflictSourceNoteGuid(
        const QString & conflictSourceNoteGuid) const;

    // Resources
    QHash<QString, qevercloud::Resource> resources() const;

    bool setResource(Resource & resource, ErrorString & errorDescription);
    const Resource * findResource(const QString & guid) const;
    bool removeResource(const QString & guid);

    // Linked notebooks
    QHash<QString, qevercloud::LinkedNotebook> linkedNotebooks() const;

    bool setLinkedNotebook(
        LinkedNotebook & linkedNotebook, ErrorString & errorDescription);

    const LinkedNotebook * findLinkedNotebook(const QString & guid) const;
    bool removeLinkedNotebook(const QString & guid);

    void setExpungedLinkedNotebookGuid(const QString & guid);
    bool containsExpungedLinkedNotebookGuid(const QString & guid) const;
    bool removeExpungedLinkedNotebookGuid(const QString & guid);

    // Other
    quint32 maxNumSavedSearches() const;
    void setMaxNumSavedSearches(const quint32 maxNumSavedSearches);

    quint32 maxNumTags() const;
    void setMaxNumTags(const quint32 maxNumTags);

    quint32 maxNumNotebooks() const;
    void setMaxNumNotebooks(const quint32 maxNumNotebooks);

    quint32 maxNumNotes() const;
    void setMaxNumNotes(const quint32 maxNumNotes);

    quint64 maxNoteSize() const;
    void setMaxNoteSize(const quint64 maxNoteSize);

    quint32 maxNumResourcesPerNote() const;
    void setMaxNumResourcesPerNote(const quint32 maxNumResourcesPerNote);

    quint32 maxNumTagsPerNote() const;
    void setMaxNumTagsPerNote(const quint32 maxNumTagsPerNote);

    quint64 maxResourceSize() const;
    void setMaxResourceSize(const quint64 maxResourceSize);

    QString linkedNotebookAuthTokenForNotebook(
        const QString & notebookGuid) const;

    void setLinkedNotebookAuthTokenForNotebook(
        const QString & notebookGuid, const QString & linkedNotebookAuthToken);

    qevercloud::SyncState syncState() const;
    void setSyncState(const qevercloud::SyncState & syncState);

    const qevercloud::SyncState * findLinkedNotebookSyncState(
        const QString & linkedNotebookOwner) const;

    void setLinkedNotebookSyncState(
        const QString & linkedNotebookOwner,
        const qevercloud::SyncState & syncState);

    bool removeLinkedNotebookSyncState(const QString & linkedNotebookOwner);

    const QString & authToken() const;
    void setAuthToken(const QString & authToken);

    QString linkedNotebookAuthToken(const QString & linkedNotebookOwner) const;

    void setLinkedNotebookAuthToken(
        const QString & linkedNotebookOwner,
        const QString & linkedNotebookAuthToken);

    bool removeLinkedNotebookAuthToken(const QString & linkedNotebookOwner);

    qint32 currentMaxUsn(const QString & linkedNotebookGuid = {}) const;

    enum class APIRateLimitsTrigger
    {
        Never,
        OnGetUserOwnSyncStateAttempt,
        OnGetLinkedNotebookSyncStateAttempt,
        OnGetUserOwnSyncChunkAttempt,
        OnGetNoteAttemptAfterDownloadingUserOwnSyncChunks,
        OnGetResourceAttemptAfterDownloadingUserOwnSyncChunks,
        OnGetLinkedNotebookSyncChunkAttempt,
        OnGetNoteAttemptAfterDownloadingLinkedNotebookSyncChunks,
        OnGetResourceAttemptAfterDownloadingLinkedNotebookSyncChunks,
        OnCreateSavedSearchAttempt,
        OnUpdateSavedSearchAttempt,
        OnCreateTagAttempt,
        OnUpdateTagAttempt,
        OnCreateNotebookAttempt,
        OnUpdateNotebookAttempt,
        OnCreateNoteAttempt,
        OnUpdateNoteAttempt,
        OnAuthenticateToSharedNotebookAttempt
    };

    APIRateLimitsTrigger apiRateLimitsTrigger() const;

    void setAPIRateLimitsExceedingTrigger(const APIRateLimitsTrigger trigger);

    void considerAllExistingDataItemsSentBeforeRateLimitBreach();

    qint32 smallestUsnOfNotCompletelySentDataItemBeforeRateLimitBreach(
        const QString & linkedNotebookGuid = {}) const;

    qint32 maxUsnBeforeAPIRateLimitsExceeding(
        const QString & linkedNotebookGuid = {}) const;

public:
    // INoteStore interface
    virtual INoteStore * create() const override;

    virtual QString noteStoreUrl() const override;

    virtual void setNoteStoreUrl(QString noteStoreUrl) override;

    virtual void setAuthData(
        QString authenticationToken, QList<QNetworkCookie> cookies) override;

    virtual void stop() override;

    virtual qint32 createNotebook(
        Notebook & notebook, ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    virtual qint32 updateNotebook(
        Notebook & notebook, ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    virtual qint32 createNote(
        Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    virtual qint32 updateNote(
        Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    virtual qint32 createTag(
        Tag & tag, ErrorString & errorDescription, qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    virtual qint32 updateTag(
        Tag & tag, ErrorString & errorDescription, qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    virtual qint32 createSavedSearch(
        SavedSearch & savedSearch, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    virtual qint32 updateSavedSearch(
        SavedSearch & savedSearch, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    virtual qint32 getSyncState(
        qevercloud::SyncState & syncState, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    virtual qint32 getSyncChunk(
        const qint32 afterUSN, const qint32 maxEntries,
        const qevercloud::SyncChunkFilter & filter,
        qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    virtual qint32 getLinkedNotebookSyncState(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const QString & authToken, qevercloud::SyncState & syncState,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) override;

    virtual qint32 getLinkedNotebookSyncChunk(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const qint32 afterUSN, const qint32 maxEntries,
        const QString & linkedNotebookAuthToken, const bool fullSyncOnly,
        qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    virtual qint32 getNote(
        const bool withContent, const bool withResourcesData,
        const bool withResourcesRecognition,
        const bool withResourcesAlternateData, Note & note,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) override;

    virtual bool getNoteAsync(
        const bool withContent, const bool withResourcesData,
        const bool withResourcesRecognition,
        const bool withResourceAlternateData, const bool withSharedNotes,
        const bool withNoteAppDataValues, const bool withResourceAppDataValues,
        const bool withNoteLimits, const QString & noteGuid,
        const QString & authToken, ErrorString & errorDescription) override;

    virtual qint32 getResource(
        const bool withDataBody, const bool withRecognitionDataBody,
        const bool withAlternateDataBody, const bool withAttributes,
        const QString & authToken, Resource & resource,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) override;

    virtual bool getResourceAsync(
        const bool withDataBody, const bool withRecognitionDataBody,
        const bool withAlternateDataBody, const bool withAttributes,
        const QString & resourceGuid, const QString & authToken,
        ErrorString & errorDescription) override;

    virtual qint32 authenticateToSharedNotebook(
        const QString & shareKey, qevercloud::AuthenticationResult & authResult,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) override;

private:
    virtual void timerEvent(QTimerEvent * event) override;

private:
    qint32 checkNotebookFields(
        const Notebook & notebook, ErrorString & errorDescription) const;

    enum class CheckNoteFieldsPurpose
    {
        CreateNote = 0,
        UpdateNote
    };

    qint32 checkNoteFields(
        const Note & note, const CheckNoteFieldsPurpose purpose,
        ErrorString & errorDescription) const;

    qint32 checkResourceFields(
        const Resource & resource, ErrorString & errorDescription) const;

    qint32 checkTagFields(
        const Tag & tag, ErrorString & errorDescription) const;

    qint32 checkSavedSearchFields(
        const SavedSearch & savedSearch, ErrorString & errorDescription) const;

    qint32 checkLinkedNotebookFields(
        const qevercloud::LinkedNotebook & linkedNotebook,
        ErrorString & errorDescription) const;

    qint32 checkAppData(
        const qevercloud::LazyMap & appData,
        ErrorString & errorDescription) const;

    qint32 checkAppDataKey(
        const QString & key, const QRegExp & keyRegExp,
        ErrorString & errorDescription) const;

    qint32 checkLinkedNotebookAuthToken(
        const LinkedNotebook & linkedNotebook,
        const QString & linkedNotebookAuthToken,
        ErrorString & errorDescription) const;

    qint32 checkLinkedNotebookAuthTokenForNotebook(
        const QString & notebookGuid, const QString & linkedNotebookAuthToken,
        ErrorString & errorDescription) const;

    qint32 checkLinkedNotebookAuthTokenForTag(
        const Tag & tag, const QString & linkedNotebookAuthToken,
        ErrorString & errorDescription) const;

    /**
     * Generates next presumably unoccupied name by appending _ and number to
     * the end of the original name or increasing the number if it's already
     * there
     */
    QString nextName(const QString & name) const;

    qint32 getSyncChunkImpl(
        const qint32 afterUSN, const qint32 maxEntries, const bool fullSyncOnly,
        const QString & linkedNotebookGuid,
        const qevercloud::SyncChunkFilter & filter,
        qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription);

    void considerAllExistingDataItemsSentBeforeRateLimitBreachImpl(
        const QString & linkedNotebookGuid = QString());

    void storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
    void storeCurrentMaxUsnAsThatBeforeRateLimitBreachImpl(
        const QString & linkedNotebookGuid = QString());

    /**
     * Helper method to advance the iterator of UsnIndex to the next item with
     * larger usn and with the same linked notebook belonging as the source item
     */
    template <class ConstIterator, class UsnIndex>
    ConstIterator advanceIterator(
        ConstIterator it, const UsnIndex & index,
        const QString & linkedNotebookGuid) const;

private:
    // Saved searches store
    struct SavedSearchByGuid
    {};
    struct SavedSearchByUSN
    {};
    struct SavedSearchByNameUpper
    {};

    struct SavedSearchNameUpperExtractor
    {
        static QString nameUpper(const SavedSearch & search)
        {
            return search.name().toUpper();
        }
    };

    using SavedSearchData = boost::multi_index_container<
        SavedSearch,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<SavedSearchByGuid>,
                boost::multi_index::const_mem_fun<
                    SavedSearch, const QString &, &SavedSearch::guid>>,
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<SavedSearchByUSN>,
                boost::multi_index::const_mem_fun<
                    SavedSearch, qint32, &SavedSearch::updateSequenceNumber>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<SavedSearchByNameUpper>,
                boost::multi_index::global_fun<
                    const SavedSearch &, QString,
                    &SavedSearchNameUpperExtractor::nameUpper>>>>;

    using SavedSearchDataByGuid =
        SavedSearchData::index<SavedSearchByGuid>::type;

    using SavedSearchDataByUSN = SavedSearchData::index<SavedSearchByUSN>::type;

    using SavedSearchDataByNameUpper =
        SavedSearchData::index<SavedSearchByNameUpper>::type;

    // Tag store
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

    struct TagNameUpperExtractor
    {
        static QString nameUpper(const Tag & tag)
        {
            return tag.name().toUpper();
        }
    };

    struct TagParentTagGuidExtractor
    {
        static QString parentTagGuid(const Tag & tag)
        {
            if (!tag.hasParentGuid()) {
                return QString();
            }

            return tag.parentGuid();
        }
    };

    struct TagLinkedNotebookGuidExtractor
    {
        static QString linkedNotebookGuid(const Tag & tag)
        {
            if (!tag.hasLinkedNotebookGuid()) {
                return QString();
            }

            return tag.linkedNotebookGuid();
        }
    };

    using TagData = boost::multi_index_container<
        Tag,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<TagByGuid>,
                boost::multi_index::const_mem_fun<
                    Tag, const QString &, &Tag::guid>>,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<TagByUSN>,
                boost::multi_index::const_mem_fun<
                    Tag, qint32, &Tag::updateSequenceNumber>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<TagByNameUpper>,
                boost::multi_index::global_fun<
                    const Tag &, QString, &TagNameUpperExtractor::nameUpper>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<TagByParentTagGuid>,
                boost::multi_index::global_fun<
                    const Tag &, QString,
                    &TagParentTagGuidExtractor::parentTagGuid>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<TagByLinkedNotebookGuid>,
                boost::multi_index::global_fun<
                    const Tag &, QString,
                    &TagLinkedNotebookGuidExtractor::linkedNotebookGuid>>>>;

    using TagDataByGuid = TagData::index<TagByGuid>::type;
    using TagDataByUSN = TagData::index<TagByUSN>::type;
    using TagDataByNameUpper = TagData::index<TagByNameUpper>::type;
    using TagDataByParentTagGuid = TagData::index<TagByParentTagGuid>::type;

    using TagDataByLinkedNotebookGuid =
        TagData::index<TagByLinkedNotebookGuid>::type;

    // Notebook store
    struct NotebookByGuid
    {};
    struct NotebookByUSN
    {};
    struct NotebookByNameUpper
    {};
    struct NotebookByLinkedNotebookGuid
    {};

    struct NotebookNameUpperExtractor
    {
        static QString nameUpper(const Notebook & notebook)
        {
            return notebook.name().toUpper();
        }
    };

    struct NotebookLinkedNotebookGuidExtractor
    {
        static QString linkedNotebookGuid(const Notebook & notebook)
        {
            if (!notebook.hasLinkedNotebookGuid()) {
                return QString();
            }

            return notebook.linkedNotebookGuid();
        }
    };

    using NotebookData = boost::multi_index_container<
        Notebook,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<NotebookByGuid>,
                boost::multi_index::const_mem_fun<
                    Notebook, const QString &, &Notebook::guid>>,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<NotebookByUSN>,
                boost::multi_index::const_mem_fun<
                    Notebook, qint32, &Notebook::updateSequenceNumber>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<NotebookByNameUpper>,
                boost::multi_index::global_fun<
                    const Notebook &, QString,
                    &NotebookNameUpperExtractor::nameUpper>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<NotebookByLinkedNotebookGuid>,
                boost::multi_index::global_fun<
                    const Notebook &, QString,
                    &NotebookLinkedNotebookGuidExtractor::
                        linkedNotebookGuid>>>>;

    using NotebookDataByGuid = NotebookData::index<NotebookByGuid>::type;
    using NotebookDataByUSN = NotebookData::index<NotebookByUSN>::type;

    using NotebookDataByNameUpper =
        NotebookData::index<NotebookByNameUpper>::type;

    using NotebookDataByLinkedNotebookGuid =
        NotebookData::index<NotebookByLinkedNotebookGuid>::type;

    // Note store
    struct NoteByGuid
    {};
    struct NoteByUSN
    {};
    struct NoteByNotebookGuid
    {};
    struct NoteByConflictSourceNoteGuid
    {};

    struct NoteConflictSourceNoteGuidExtractor
    {
        static QString guid(const Note & note)
        {
            if (!note.hasNoteAttributes()) {
                return QString();
            }

            const auto & noteAttributes = note.noteAttributes();
            if (!noteAttributes.conflictSourceNoteGuid.isSet()) {
                return QString();
            }

            return noteAttributes.conflictSourceNoteGuid.ref();
        }
    };

    using NoteData = boost::multi_index_container<
        Note,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<NoteByGuid>,
                boost::multi_index::const_mem_fun<
                    Note, const QString &, &Note::guid>>,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<NoteByUSN>,
                boost::multi_index::const_mem_fun<
                    Note, qint32, &Note::updateSequenceNumber>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<NoteByNotebookGuid>,
                boost::multi_index::const_mem_fun<
                    Note, const QString &, &Note::notebookGuid>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<NoteByConflictSourceNoteGuid>,
                boost::multi_index::global_fun<
                    const Note &, QString,
                    &NoteConflictSourceNoteGuidExtractor::guid>>>>;

    using NoteDataByGuid = NoteData::index<NoteByGuid>::type;
    using NoteDataByUSN = NoteData::index<NoteByUSN>::type;
    using NoteDataByNotebookGuid = NoteData::index<NoteByNotebookGuid>::type;

    using NoteDataByConflictSourceNoteGuid =
        NoteData::index<NoteByConflictSourceNoteGuid>::type;

    // Resource store
    struct ResourceByGuid
    {};
    struct ResourceByUSN
    {};
    struct ResourceByNoteGuid
    {};

    using ResourceData = boost::multi_index_container<
        Resource,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<ResourceByGuid>,
                boost::multi_index::const_mem_fun<
                    Resource, const QString &, &Resource::guid>>,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<ResourceByUSN>,
                boost::multi_index::const_mem_fun<
                    Resource, qint32, &Resource::updateSequenceNumber>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<ResourceByNoteGuid>,
                boost::multi_index::const_mem_fun<
                    Resource, const QString &, &Resource::noteGuid>>>>;

    using ResourceDataByGuid = ResourceData::index<ResourceByGuid>::type;
    using ResourceDataByUSN = ResourceData::index<ResourceByUSN>::type;

    using ResourceDataByNoteGuid =
        ResourceData::index<ResourceByNoteGuid>::type;

    // Linked notebook store
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

    struct LinkedNotebookShardIdExtractor
    {
        static QString shardId(const LinkedNotebook & linkedNotebook)
        {
            if (!linkedNotebook.hasShardId()) {
                return QString();
            }

            return linkedNotebook.shardId();
        }
    };

    struct LinkedNotebookUriExtractor
    {
        static QString uri(const LinkedNotebook & linkedNotebook)
        {
            if (!linkedNotebook.hasUri()) {
                return QString();
            }

            return linkedNotebook.uri();
        }
    };

    using LinkedNotebookData = boost::multi_index_container<
        LinkedNotebook,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<LinkedNotebookByGuid>,
                boost::multi_index::const_mem_fun<
                    LinkedNotebook, const QString &, &LinkedNotebook::guid>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<LinkedNotebookByShardId>,
                boost::multi_index::global_fun<
                    const LinkedNotebook &, QString,
                    &LinkedNotebookShardIdExtractor::shardId>>,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<LinkedNotebookByUri>,
                boost::multi_index::global_fun<
                    const LinkedNotebook &, QString,
                    &LinkedNotebookUriExtractor::uri>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<LinkedNotebookByUsername>,
                boost::multi_index::const_mem_fun<
                    LinkedNotebook, const QString &,
                    &LinkedNotebook::username>>,
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<LinkedNotebookByUSN>,
                boost::multi_index::const_mem_fun<
                    LinkedNotebook, qint32,
                    &LinkedNotebook::updateSequenceNumber>>,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<LinkedNotebookBySharedNotebookGlobalId>,
                boost::multi_index::const_mem_fun<
                    LinkedNotebook, const QString &,
                    &LinkedNotebook::sharedNotebookGlobalId>>>>;

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

    template <class T>
    class CompareByUSN
    {
    public:
        bool operator()(const qint32 usn, const T & item) const
        {
            return usn < item.updateSequenceNumber();
        }
    };

    // Enum used to help maintain the bookkeeping
    // on which kind of item should be the next one inserted into
    // the sync chunk
    enum class NextItemType
    {
        None = 0,
        SavedSearch,
        Tag,
        Notebook,
        Note,
        Resource,
        LinkedNotebook
    };

    friend QDebug & operator<<(QDebug & dbg, const NextItemType nextItemType);

    // Struct encapsulating parameters required for a single async getNote
    // request
    struct GetNoteAsyncRequest
    {
        bool m_withContent = false;
        bool m_withResourcesData = false;
        bool m_withResourcesRecognition = false;
        bool m_withResourcesAlternateData = false;
        bool m_withSharedNotes = false;
        bool m_withNoteAppDataValues = false;
        bool m_withResourceAppDataValues = false;
        bool m_withNoteLimits = false;

        QString m_noteGuid;
        QString m_authToken;
    };

    // Struct encapsulating parameters required for a single async getResource
    // request
    struct GetResourceAsyncRequest
    {
        bool m_withDataBody = false;
        bool m_withRecognitionDataBody = false;
        bool m_withAlternateDataBody = false;
        bool m_withAttributes = false;

        QString m_resourceGuid;
        QString m_authToken;
    };

    // Struct serving as a collection of guids of items
    // which were sent to the agent synchronizing with FakeNoteStore
    // in their entirety i.e. while saved searches, tags, notebooks, linked
    // notebooks are counted as they are sent within sync chunks, notes and
    // resources are only counted when their whole contents are requested and
    // sent to the synchronizing agent
    struct GuidsOfCompleteSentItems
    {
        QSet<QString> m_savedSearchGuids;
        QSet<QString> m_tagGuids;
        QSet<QString> m_notebookGuids;
        QSet<QString> m_linkedNotebookGuids;
        QSet<QString> m_noteGuids;
        QSet<QString> m_resourceGuids;
    };

private:
    NoteDataByUSN::const_iterator nextNoteByUsnIterator(
        NoteDataByUSN::const_iterator it,
        const QString & targetLinkedNotebookGuid = {}) const;

    ResourceDataByUSN::const_iterator nextResourceByUsnIterator(
        ResourceDataByUSN::const_iterator it,
        const QString & targetLinkedNotebookGuid = {}) const;

private:
    struct Data
    {
        SavedSearchData m_savedSearches;
        QSet<QString> m_expungedSavedSearchGuids;

        TagData m_tags;
        QSet<QString> m_expungedTagGuids;

        NotebookData m_notebooks;
        QSet<QString> m_expungedNotebookGuids;

        NoteData m_notes;
        QSet<QString> m_expungedNoteGuids;

        ResourceData m_resources;

        LinkedNotebookData m_linkedNotebooks;
        QSet<QString> m_expungedLinkedNotebookGuids;

        bool m_onceGetLinkedNotebookSyncChunkCalled = false;
        bool m_onceAPIRateLimitExceedingTriggered = false;

        APIRateLimitsTrigger m_APIRateLimitsTrigger =
            APIRateLimitsTrigger::Never;

        QSet<int> m_getNoteAsyncDelayTimerIds;
        QSet<int> m_getResourceAsyncDelayTimerIds;

        quint32 m_maxNumSavedSearches =
            static_cast<quint32>(qevercloud::EDAM_USER_SAVED_SEARCHES_MAX);

        quint32 m_maxNumTags =
            static_cast<quint32>(qevercloud::EDAM_USER_TAGS_MAX);

        quint32 m_maxNumNotebooks =
            static_cast<quint32>(qevercloud::EDAM_USER_NOTEBOOKS_MAX);

        quint32 m_maxNumNotes =
            static_cast<quint32>(qevercloud::EDAM_USER_NOTES_MAX);

        quint64 m_maxNoteSize =
            static_cast<quint64>(qevercloud::EDAM_NOTE_SIZE_MAX_FREE);

        quint32 m_maxNumResourcesPerNote =
            static_cast<quint32>(qevercloud::EDAM_NOTE_RESOURCES_MAX);

        quint32 m_maxNumTagsPerNote =
            static_cast<quint32>(qevercloud::EDAM_NOTE_TAGS_MAX);

        quint64 m_maxResourceSize =
            static_cast<quint64>(qevercloud::EDAM_RESOURCE_SIZE_MAX_FREE);

        qevercloud::SyncState m_syncState;
        QHash<QString, qevercloud::SyncState> m_linkedNotebookSyncStates;

        GuidsOfCompleteSentItems m_guidsOfUserOwnCompleteSentItems;
        QHash<QString, GuidsOfCompleteSentItems>
            m_guidsOfCompleteSentItemsByLinkedNotebookGuid;

        qint32 m_maxUsnForUserOwnDataBeforeRateLimitBreach = 0;
        QHash<QString, qint32>
            m_maxUsnsForLinkedNotebooksDataBeforeRateLimitBreach;

        QString m_noteStoreUrl;

        QString m_authenticationToken;
        QHash<QString, QString> m_linkedNotebookAuthTokens;

        QQueue<GetNoteAsyncRequest> m_getNoteAsyncRequests;
        QQueue<GetResourceAsyncRequest> m_getResourceAsyncRequests;
    };

    FakeNoteStore(std::shared_ptr<Data> data);

    std::shared_ptr<Data> m_data;
};

using FakeNoteStorePtr = std::shared_ptr<FakeNoteStore>;

} // namespace quentier

#endif // LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_NOTE_STORE_H
