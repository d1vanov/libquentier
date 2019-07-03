/*
 * Copyright 2018-2019 Dmitry Ivanov
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

#include <quentier_private/synchronization/INoteStore.h>
#include <quentier/types/SavedSearch.h>
#include <quentier/types/Tag.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/Note.h>
#include <quentier/types/Resource.h>
#include <quentier/types/LinkedNotebook.h>
#include <quentier/utility/Macros.h>

// NOTE: Workaround a bug in Qt4 which may prevent building with some boost versions
#ifndef Q_MOC_RUN
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/bimap.hpp>
#endif

#include <QSharedPointer>
#include <QSet>
#include <QHash>
#include <QQueue>

namespace quentier {

class FakeNoteStore: public INoteStore
{
    Q_OBJECT
public:
    explicit FakeNoteStore(QObject * parent = Q_NULLPTR);

    // Saved searches
    QHash<QString,qevercloud::SavedSearch> savedSearches() const;

    bool setSavedSearch(SavedSearch & search, ErrorString & errorDescription);
    const SavedSearch * findSavedSearch(const QString & guid) const;
    bool removeSavedSearch(const QString & guid);

    void setExpungedSavedSearchGuid(const QString & guid);
    bool containsExpungedSavedSearchGuid(const QString & guid) const;
    bool removeExpungedSavedSearchGuid(const QString & guid);

    // Tags
    QHash<QString,qevercloud::Tag> tags() const;

    bool setTag(Tag & tag, ErrorString & errorDescription);
    const Tag * findTag(const QString & guid) const;
    bool removeTag(const QString & guid);

    void setExpungedTagGuid(const QString & guid);
    bool containsExpungedTagGuid(const QString & guid) const;
    bool removeExpungedTagGuid(const QString & guid);

    // Notebooks
    QHash<QString,qevercloud::Notebook> notebooks() const;

    bool setNotebook(Notebook & notebook, ErrorString & errorDescription);
    const Notebook * findNotebook(const QString & guid) const;
    bool removeNotebook(const QString & guid);

    QList<const Notebook*> findNotebooksForLinkedNotebookGuid(
        const QString & linkedNotebookGuid) const;

    void setExpungedNotebookGuid(const QString & guid);
    bool containsExpungedNotebookGuid(const QString & guid) const;
    bool removeExpungedNotebookGuid(const QString & guid);

    // Notes
    QHash<QString,qevercloud::Note> notes() const;

    bool setNote(Note & note, ErrorString & errorDescription);
    const Note * findNote(const QString & guid) const;
    bool removeNote(const QString & guid);

    void setExpungedNoteGuid(const QString & guid);
    bool containsExpungedNoteGuid(const QString & guid) const;
    bool removeExpungedNoteGuid(const QString & guid);

    QList<Note> getNotesByConflictSourceNoteGuid(
        const QString & conflictSourceNoteGuid) const;

    // Resources
    QHash<QString,qevercloud::Resource> resources() const;

    bool setResource(Resource & resource, ErrorString & errorDescription);
    const Resource * findResource(const QString & guid) const;
    bool removeResource(const QString & guid);

    // Linked notebooks
    QHash<QString,qevercloud::LinkedNotebook> linkedNotebooks() const;

    bool setLinkedNotebook(LinkedNotebook & linkedNotebook,
                           ErrorString & errorDescription);
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

    QString linkedNotebookAuthTokenForNotebook(const QString & notebookGuid) const;
    void setLinkedNotebookAuthTokenForNotebook(const QString & notebookGuid,
                                               const QString & linkedNotebookAuthToken);

    qevercloud::SyncState syncState() const;
    void setSyncState(const qevercloud::SyncState & syncState);

    const qevercloud::SyncState * findLinkedNotebookSyncState(
        const QString & linkedNotebookOwner) const;
    void setLinkedNotebookSyncState(const QString & linkedNotebookOwner,
                                    const qevercloud::SyncState & syncState);
    bool removeLinkedNotebookSyncState(const QString & linkedNotebookOwner);

    const QString & authToken() const;
    void setAuthToken(const QString & authToken);

    QString linkedNotebookAuthToken(const QString & linkedNotebookOwner) const;
    void setLinkedNotebookAuthToken(const QString & linkedNotebookOwner,
                                    const QString & linkedNotebookAuthToken);
    bool removeLinkedNotebookAuthToken(const QString & linkedNotebookOwner);

    qint32 currentMaxUsn(const QString & linkedNotebookGuid = QString()) const;

    // API rate limits handling
    struct WhenToTriggerAPIRateLimitsExceeding
    {
        enum type
        {
            Never = 0,
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
    };

    WhenToTriggerAPIRateLimitsExceeding::type whenToTriggerAPIRateLimitsExceeding() const;
    void setAPIRateLimitsExceedingTrigger(
        const WhenToTriggerAPIRateLimitsExceeding::type trigger);

    void considerAllExistingDataItemsSentBeforeRateLimitBreach();
    qint32 smallestUsnOfNotCompletelySentDataItemBeforeRateLimitBreach(
        const QString & linkedNotebookGuid = QString()) const;

    qint32 maxUsnBeforeAPIRateLimitsExceeding(const QString & linkedNotebookGuid = QString()) const;

public:
    // INoteStore interface
    virtual INoteStore * create() const Q_DECL_OVERRIDE;
    virtual void stop() Q_DECL_OVERRIDE;
    virtual qint32 createNotebook(Notebook & notebook,
                                  ErrorString & errorDescription,
                                  qint32 & rateLimitSeconds,
                                  const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;
    virtual qint32 updateNotebook(Notebook & notebook,
                                  ErrorString & errorDescription,
                                  qint32 & rateLimitSeconds,
                                  const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;
    virtual qint32 createNote(Note & note,
                              ErrorString & errorDescription,
                              qint32 & rateLimitSeconds,
                              const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;
    virtual qint32 updateNote(Note & note,
                              ErrorString & errorDescription,
                              qint32 & rateLimitSeconds,
                              const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;
    virtual qint32 createTag(Tag & tag,
                             ErrorString & errorDescription,
                             qint32 & rateLimitSeconds,
                             const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;
    virtual qint32 updateTag(Tag & tag,
                             ErrorString & errorDescription,
                             qint32 & rateLimitSeconds,
                             const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;
    virtual qint32 createSavedSearch(SavedSearch & savedSearch,
                                     ErrorString & errorDescription,
                                     qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;
    virtual qint32 updateSavedSearch(SavedSearch & savedSearch,
                                     ErrorString & errorDescription,
                                     qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;
    virtual qint32 getSyncState(qevercloud::SyncState & syncState,
                                ErrorString & errorDescription,
                                qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;
    virtual qint32 getSyncChunk(const qint32 afterUSN, const qint32 maxEntries,
                                const qevercloud::SyncChunkFilter & filter,
                                qevercloud::SyncChunk & syncChunk,
                                ErrorString & errorDescription,
                                qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;

    virtual qint32 getLinkedNotebookSyncState(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const QString & authToken, qevercloud::SyncState & syncState,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;

    virtual qint32 getLinkedNotebookSyncChunk(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const qint32 afterUSN, const qint32 maxEntries,
        const QString & linkedNotebookAuthToken, const bool fullSyncOnly,
        qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;

    virtual qint32 getNote(const bool withContent,
                           const bool withResourcesData,
                           const bool withResourcesRecognition,
                           const bool withResourcesAlternateData,
                           Note & note, ErrorString & errorDescription,
                           qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;
    virtual bool getNoteAsync(const bool withContent,
                              const bool withResourcesData,
                              const bool withResourcesRecognition,
                              const bool withResourceAlternateData,
                              const bool withSharedNotes,
                              const bool withNoteAppDataValues,
                              const bool withResourceAppDataValues,
                              const bool withNoteLimits,
                              const QString & noteGuid,
                              const QString & authToken,
                              ErrorString & errorDescription) Q_DECL_OVERRIDE;
    virtual qint32 getResource(const bool withDataBody,
                               const bool withRecognitionDataBody,
                               const bool withAlternateDataBody,
                               const bool withAttributes,
                               const QString & authToken,
                               Resource & resource,
                               ErrorString & errorDescription,
                               qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;
    virtual bool getResourceAsync(const bool withDataBody,
                                  const bool withRecognitionDataBody,
                                  const bool withAlternateDataBody,
                                  const bool withAttributes,
                                  const QString & resourceGuid,
                                  const QString & authToken,
                                  ErrorString & errorDescription) Q_DECL_OVERRIDE;
    virtual qint32 authenticateToSharedNotebook(
        const QString & shareKey,
        qevercloud::AuthenticationResult & authResult,
        ErrorString & errorDescription,
        qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;

private:
    virtual void timerEvent(QTimerEvent * event) Q_DECL_OVERRIDE;

private:
    qint32 checkNotebookFields(const Notebook & notebook,
                               ErrorString & errorDescription) const;

    struct CheckNoteFieldsPurpose
    {
        enum type
        {
            CreateNote = 0,
            UpdateNote
        };
    };

    qint32 checkNoteFields(const Note & note,
                           const CheckNoteFieldsPurpose::type purpose,
                           ErrorString & errorDescription) const;
    qint32 checkResourceFields(const Resource & resource,
                               ErrorString & errorDescription) const;
    qint32 checkTagFields(const Tag & tag, ErrorString & errorDescription) const;
    qint32 checkSavedSearchFields(const SavedSearch & savedSearch,
                                  ErrorString & errorDescription) const;

    qint32 checkLinkedNotebookFields(const qevercloud::LinkedNotebook & linkedNotebook,
                                     ErrorString & errorDescription) const;

    qint32 checkAppData(const qevercloud::LazyMap & appData,
                        ErrorString & errorDescription) const;
    qint32 checkAppDataKey(const QString & key, const QRegExp & keyRegExp,
                           ErrorString & errorDescription) const;

    qint32 checkLinkedNotebookAuthToken(const LinkedNotebook & linkedNotebook,
                                        const QString & linkedNotebookAuthToken,
                                        ErrorString & errorDescription) const;
    qint32 checkLinkedNotebookAuthTokenForNotebook(const QString & notebookGuid,
                                                   const QString & linkedNotebookAuthToken,
                                                   ErrorString & errorDescription) const;
    qint32 checkLinkedNotebookAuthTokenForTag(const Tag & tag,
                                              const QString & linkedNotebookAuthToken,
                                              ErrorString & errorDescription) const;

    /**
     * Generates next presumably unoccupied name by appending _ and number to
     * the end of the original name or increasing the number if it's already
     * there
     */
    QString nextName(const QString & name) const;

    qint32 getSyncChunkImpl(const qint32 afterUSN, const qint32 maxEntries,
                            const bool fullSyncOnly,
                            const QString & linkedNotebookGuid,
                            const qevercloud::SyncChunkFilter & filter,
                            qevercloud::SyncChunk & syncChunk,
                            ErrorString & errorDescription);

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
    ConstIterator advanceIterator(ConstIterator it, const UsnIndex & index,
                                  const QString & linkedNotebookGuid) const;

private:
    // Saved searches store
    struct SavedSearchByGuid{};
    struct SavedSearchByUSN{};
    struct SavedSearchByNameUpper{};

    struct SavedSearchNameUpperExtractor
    {
        static QString nameUpper(const SavedSearch & search)
        {
            return search.name().toUpper();
        }
    };

    typedef boost::multi_index_container<
        SavedSearch,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<SavedSearchByGuid>,
                boost::multi_index::const_mem_fun<
                    SavedSearch,const QString&,&SavedSearch::guid>
            >,
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<SavedSearchByUSN>,
                boost::multi_index::const_mem_fun<
                    SavedSearch,qint32,&SavedSearch::updateSequenceNumber>
            >,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<SavedSearchByNameUpper>,
                boost::multi_index::global_fun<
                    const SavedSearch&,QString,&SavedSearchNameUpperExtractor::nameUpper>
            >
        >
    > SavedSearchData;

    typedef SavedSearchData::index<SavedSearchByGuid>::type SavedSearchDataByGuid;
    typedef SavedSearchData::index<SavedSearchByUSN>::type SavedSearchDataByUSN;
    typedef SavedSearchData::index<SavedSearchByNameUpper>::type SavedSearchDataByNameUpper;

    // Tag store
    struct TagByGuid{};
    struct TagByUSN{};
    struct TagByNameUpper{};
    struct TagByParentTagGuid{};
    struct TagByLinkedNotebookGuid{};

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

    typedef boost::multi_index_container<
        Tag,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<TagByGuid>,
                boost::multi_index::const_mem_fun<Tag,const QString&,&Tag::guid>
            >,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<TagByUSN>,
                boost::multi_index::const_mem_fun<Tag,qint32,&Tag::updateSequenceNumber>
            >,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<TagByNameUpper>,
                boost::multi_index::global_fun<
                    const Tag&,QString,&TagNameUpperExtractor::nameUpper>
            >,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<TagByParentTagGuid>,
                boost::multi_index::global_fun<
                    const Tag&,QString,&TagParentTagGuidExtractor::parentTagGuid>
            >,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<TagByLinkedNotebookGuid>,
                boost::multi_index::global_fun<
                    const Tag&,QString,&TagLinkedNotebookGuidExtractor::linkedNotebookGuid>
            >
        >
    > TagData;

    typedef TagData::index<TagByGuid>::type TagDataByGuid;
    typedef TagData::index<TagByUSN>::type TagDataByUSN;
    typedef TagData::index<TagByNameUpper>::type TagDataByNameUpper;
    typedef TagData::index<TagByParentTagGuid>::type TagDataByParentTagGuid;
    typedef TagData::index<TagByLinkedNotebookGuid>::type TagDataByLinkedNotebookGuid;

    // Notebook store
    struct NotebookByGuid{};
    struct NotebookByUSN{};
    struct NotebookByNameUpper{};
    struct NotebookByLinkedNotebookGuid{};

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

    typedef boost::multi_index_container<
        Notebook,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<NotebookByGuid>,
                boost::multi_index::const_mem_fun<
                    Notebook,const QString&,&Notebook::guid>
            >,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<NotebookByUSN>,
                boost::multi_index::const_mem_fun<
                    Notebook,qint32,&Notebook::updateSequenceNumber>
            >,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<NotebookByNameUpper>,
                boost::multi_index::global_fun<
                    const Notebook&,QString,
                    &NotebookNameUpperExtractor::nameUpper>
            >,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<NotebookByLinkedNotebookGuid>,
                boost::multi_index::global_fun<
                    const Notebook&,QString,
                    &NotebookLinkedNotebookGuidExtractor::linkedNotebookGuid>
            >
        >
    > NotebookData;

    typedef NotebookData::index<NotebookByGuid>::type NotebookDataByGuid;
    typedef NotebookData::index<NotebookByUSN>::type NotebookDataByUSN;
    typedef NotebookData::index<NotebookByNameUpper>::type NotebookDataByNameUpper;
    typedef NotebookData::index<NotebookByLinkedNotebookGuid>::type NotebookDataByLinkedNotebookGuid;

    // Note store
    struct NoteByGuid{};
    struct NoteByUSN{};
    struct NoteByNotebookGuid{};
    struct NoteByConflictSourceNoteGuid{};

    struct NoteConflictSourceNoteGuidExtractor
    {
        static QString guid(const Note & note)
        {
            if (!note.hasNoteAttributes()) {
                return QString();
            }

            const qevercloud::NoteAttributes & noteAttributes = note.noteAttributes();
            if (!noteAttributes.conflictSourceNoteGuid.isSet()) {
                return QString();
            }

            return noteAttributes.conflictSourceNoteGuid.ref();
        }
    };

    typedef boost::multi_index_container<
        Note,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<NoteByGuid>,
                boost::multi_index::const_mem_fun<
                    Note,const QString&,&Note::guid>
            >,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<NoteByUSN>,
                boost::multi_index::const_mem_fun<
                    Note,qint32,&Note::updateSequenceNumber>
            >,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<NoteByNotebookGuid>,
                boost::multi_index::const_mem_fun<
                    Note,const QString&,&Note::notebookGuid>
            >,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<NoteByConflictSourceNoteGuid>,
                boost::multi_index::global_fun<
                    const Note &,QString,
                &NoteConflictSourceNoteGuidExtractor::guid>
            >
        >
    > NoteData;

    typedef NoteData::index<NoteByGuid>::type NoteDataByGuid;
    typedef NoteData::index<NoteByUSN>::type NoteDataByUSN;
    typedef NoteData::index<NoteByNotebookGuid>::type NoteDataByNotebookGuid;
    typedef NoteData::index<NoteByConflictSourceNoteGuid>::type NoteDataByConflictSourceNoteGuid;

    // Resource store
    struct ResourceByGuid{};
    struct ResourceByUSN{};
    struct ResourceByNoteGuid{};

    typedef boost::multi_index_container<
        Resource,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<ResourceByGuid>,
                boost::multi_index::const_mem_fun<
                    Resource,const QString&,&Resource::guid>
            >,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<ResourceByUSN>,
                boost::multi_index::const_mem_fun<
                    Resource,qint32,&Resource::updateSequenceNumber>
            >,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<ResourceByNoteGuid>,
                boost::multi_index::const_mem_fun<
                    Resource,const QString&,&Resource::noteGuid>
            >
        >
    > ResourceData;

    typedef ResourceData::index<ResourceByGuid>::type ResourceDataByGuid;
    typedef ResourceData::index<ResourceByUSN>::type ResourceDataByUSN;
    typedef ResourceData::index<ResourceByNoteGuid>::type ResourceDataByNoteGuid;

    // Linked notebook store
    struct LinkedNotebookByGuid{};
    struct LinkedNotebookByUSN{};
    struct LinkedNotebookByShardId{};
    struct LinkedNotebookByUri{};
    struct LinkedNotebookByUsername{};
    struct LinkedNotebookBySharedNotebookGlobalId{};

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

    typedef boost::multi_index_container<
        LinkedNotebook,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<LinkedNotebookByGuid>,
                boost::multi_index::const_mem_fun<
                    LinkedNotebook,const QString&,&LinkedNotebook::guid>
            >,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<LinkedNotebookByShardId>,
                boost::multi_index::global_fun<
                    const LinkedNotebook&,QString,
                    &LinkedNotebookShardIdExtractor::shardId>
            >,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<LinkedNotebookByUri>,
                boost::multi_index::global_fun<
                    const LinkedNotebook&,QString,
                    &LinkedNotebookUriExtractor::uri>
            >,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<LinkedNotebookByUsername>,
                boost::multi_index::const_mem_fun<
                    LinkedNotebook,const QString&,&LinkedNotebook::username>
            >,
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<LinkedNotebookByUSN>,
                boost::multi_index::const_mem_fun<
                    LinkedNotebook,qint32,
                    &LinkedNotebook::updateSequenceNumber>
            >,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<LinkedNotebookBySharedNotebookGlobalId>,
                boost::multi_index::const_mem_fun<
                    LinkedNotebook,const QString&,
                    &LinkedNotebook::sharedNotebookGlobalId>
            >
        >
    > LinkedNotebookData;

    typedef LinkedNotebookData::index<LinkedNotebookByGuid>::type LinkedNotebookDataByGuid;
    typedef LinkedNotebookData::index<LinkedNotebookByUSN>::type LinkedNotebookDataByUSN;
    typedef LinkedNotebookData::index<LinkedNotebookByShardId>::type LinkedNotebookDataByShardId;
    typedef LinkedNotebookData::index<LinkedNotebookByUri>::type LinkedNotebookDataByUri;
    typedef LinkedNotebookData::index<LinkedNotebookByUsername>::type LinkedNotebookDataByUsername;

    typedef LinkedNotebookData::index<LinkedNotebookBySharedNotebookGlobalId>::type
        LinkedNotebookDataBySharedNotebookGlobalId;

    template <class T>
    class CompareByUSN
    {
    public:
        bool operator()(const qint32 usn, const T & item) const
        {
            return usn < item.updateSequenceNumber();
        }
    };

    // C++98 style scoped enum used to help maintain the bookkeeping
    // on which kind of item should be the next one inserted into
    // the sync chunk
    struct NextItemType
    {
        enum type
        {
            None = 0,
            SavedSearch,
            Tag,
            Notebook,
            Note,
            Resource,
            LinkedNotebook
        };
    };

    // Struct encapsulating parameters required for a single async getNote request
    struct GetNoteAsyncRequest
    {
        GetNoteAsyncRequest() :
            m_withContent(false),
            m_withResourcesData(false),
            m_withResourcesRecognition(false),
            m_withResourcesAlternateData(false),
            m_withSharedNotes(false),
            m_withNoteAppDataValues(false),
            m_withResourceAppDataValues(false),
            m_withNoteLimits(false),
            m_noteGuid(),
            m_authToken()
        {}

        bool    m_withContent;
        bool    m_withResourcesData;
        bool    m_withResourcesRecognition;
        bool    m_withResourcesAlternateData;
        bool    m_withSharedNotes;
        bool    m_withNoteAppDataValues;
        bool    m_withResourceAppDataValues;
        bool    m_withNoteLimits;
        QString m_noteGuid;
        QString m_authToken;
    };

    // Struct encapsulating parameters required for a single async getResource request
    struct GetResourceAsyncRequest
    {
        GetResourceAsyncRequest() :
            m_withDataBody(false),
            m_withRecognitionDataBody(false),
            m_withAlternateDataBody(false),
            m_withAttributes(false),
            m_resourceGuid(),
            m_authToken()
        {}

        bool    m_withDataBody;
        bool    m_withRecognitionDataBody;
        bool    m_withAlternateDataBody;
        bool    m_withAttributes;
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
        QSet<QString>   m_savedSearchGuids;
        QSet<QString>   m_tagGuids;
        QSet<QString>   m_notebookGuids;
        QSet<QString>   m_linkedNotebookGuids;
        QSet<QString>   m_noteGuids;
        QSet<QString>   m_resourceGuids;
    };

private:
    NoteDataByUSN::const_iterator
    nextNoteByUsnIterator(NoteDataByUSN::const_iterator it,
                          const QString & targetLinkedNotebookGuid = QString()) const;
    ResourceDataByUSN::const_iterator
    nextResourceByUsnIterator(ResourceDataByUSN::const_iterator it,
                              const QString & targetLinkedNotebookGuid = QString()) const;

private:
    struct Data
    {
        Data();

        SavedSearchData         m_savedSearches;
        QSet<QString>           m_expungedSavedSearchGuids;

        TagData                 m_tags;
        QSet<QString>           m_expungedTagGuids;

        NotebookData            m_notebooks;
        QSet<QString>           m_expungedNotebookGuids;

        NoteData                m_notes;
        QSet<QString>           m_expungedNoteGuids;

        ResourceData            m_resources;

        LinkedNotebookData      m_linkedNotebooks;
        QSet<QString>           m_expungedLinkedNotebookGuids;

        bool                    m_onceGetLinkedNotebookSyncChunkCalled;
        bool                    m_onceAPIRateLimitExceedingTriggered;
        WhenToTriggerAPIRateLimitsExceeding::type       m_whenToTriggerAPIRateLimitExceeding;

        QSet<int>               m_getNoteAsyncDelayTimerIds;
        QSet<int>               m_getResourceAsyncDelayTimerIds;

        quint32                 m_maxNumSavedSearches;
        quint32                 m_maxNumTags;
        quint32                 m_maxNumNotebooks;
        quint32                 m_maxNumNotes;

        quint64                 m_maxNoteSize;
        quint32                 m_maxNumResourcesPerNote;
        quint32                 m_maxNumTagsPerNote;
        quint64                 m_maxResourceSize;

        qevercloud::SyncState   m_syncState;
        QHash<QString,qevercloud::SyncState>    m_linkedNotebookSyncStates;

        GuidsOfCompleteSentItems    m_guidsOfUserOwnCompleteSentItems;
        QHash<QString, GuidsOfCompleteSentItems>    m_guidsOfCompleteSentItemsByLinkedNotebookGuid;

        qint32                  m_maxUsnForUserOwnDataBeforeRateLimitBreach;
        QHash<QString,qint32>   m_maxUsnsForLinkedNotebooksDataBeforeRateLimitBreach;

        QString                 m_authenticationToken;
        QHash<QString,QString>  m_linkedNotebookAuthTokens;

        QQueue<GetNoteAsyncRequest>     m_getNoteAsyncRequests;
        QQueue<GetResourceAsyncRequest> m_getResourceAsyncRequests;
    };

    FakeNoteStore(const QSharedPointer<Data> & data);

    QSharedPointer<Data>    m_data;
};

} // namespace quentier

inline size_t hash_value(const QString & str) { return qHash(str); }

#endif // LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_NOTE_STORE_H