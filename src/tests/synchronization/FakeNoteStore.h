/*
 * Copyright 2018-2021 Dmitry Ivanov
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
#include <quentier/utility/Compat.h>

#include <qevercloud/generated/Constants.h>
#include <qevercloud/generated/types/LinkedNotebook.h>
#include <qevercloud/generated/types/Note.h>
#include <qevercloud/generated/types/Notebook.h>
#include <qevercloud/generated/types/Resource.h>
#include <qevercloud/generated/types/SavedSearch.h>
#include <qevercloud/generated/types/SyncState.h>
#include <qevercloud/generated/types/Tag.h>

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

class FakeNoteStore final: public INoteStore
{
    Q_OBJECT
public:
    explicit FakeNoteStore(QObject * parent = nullptr);
    ~FakeNoteStore() override;

    // Saved searches
    [[nodiscard]] QHash<QString, qevercloud::SavedSearch> savedSearches() const;

    [[nodiscard]] bool setSavedSearch(
        qevercloud::SavedSearch & search, ErrorString & errorDescription);

    [[nodiscard]] const qevercloud::SavedSearch * findSavedSearch(
        const QString & guid) const;

    [[nodiscard]] bool removeSavedSearch(const QString & guid);

    void setExpungedSavedSearchGuid(const QString & guid);

    [[nodiscard]] bool containsExpungedSavedSearchGuid(
        const QString & guid) const;

    [[nodiscard]] bool removeExpungedSavedSearchGuid(const QString & guid);

    // Tags
    [[nodiscard]] QHash<QString, qevercloud::Tag> tags() const;

    [[nodiscard]] bool setTag(
        qevercloud::Tag & tag, ErrorString & errorDescription);

    [[nodiscard]] const qevercloud::Tag * findTag(const QString & guid) const;
    [[nodiscard]] bool removeTag(const QString & guid);

    void setExpungedTagGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedTagGuid(const QString & guid) const;
    [[nodiscard]] bool removeExpungedTagGuid(const QString & guid);

    // Notebooks
    [[nodiscard]] QHash<QString, qevercloud::Notebook> notebooks() const;

    [[nodiscard]] bool setNotebook(
        qevercloud::Notebook & notebook, ErrorString & errorDescription);

    [[nodiscard]] const qevercloud::Notebook * findNotebook(
        const QString & guid) const;

    [[nodiscard]] bool removeNotebook(const QString & guid);

    [[nodiscard]] QList<const qevercloud::Notebook *>
        findNotebooksForLinkedNotebookGuid(
            const QString & linkedNotebookGuid) const;

    void setExpungedNotebookGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedNotebookGuid(const QString & guid) const;
    [[nodiscard]] bool removeExpungedNotebookGuid(const QString & guid);

    // Notes
    [[nodiscard]] QHash<QString, qevercloud::Note> notes() const;

    [[nodiscard]] bool setNote(
        qevercloud::Note & note, ErrorString & errorDescription);

    [[nodiscard]] const qevercloud::Note * findNote(const QString & guid) const;
    [[nodiscard]] bool removeNote(const QString & guid);

    void setExpungedNoteGuid(const QString & guid);
    [[nodiscard]] bool containsExpungedNoteGuid(const QString & guid) const;
    [[nodiscard]] bool removeExpungedNoteGuid(const QString & guid);

    [[nodiscard]] QList<qevercloud::Note> getNotesByConflictSourceNoteGuid(
        const QString & conflictSourceNoteGuid) const;

    // Resources
    [[nodiscard]] QHash<QString, qevercloud::Resource> resources() const;

    [[nodiscard]] bool setResource(
        qevercloud::Resource & resource, ErrorString & errorDescription);

    [[nodiscard]] const qevercloud::Resource * findResource(
        const QString & guid) const;

    [[nodiscard]] bool removeResource(const QString & guid);

    // Linked notebooks
    [[nodiscard]] QHash<QString, qevercloud::LinkedNotebook>
        linkedNotebooks() const;

    [[nodiscard]] bool setLinkedNotebook(
        qevercloud::LinkedNotebook & linkedNotebook,
        ErrorString & errorDescription);

    [[nodiscard]] const qevercloud::LinkedNotebook * findLinkedNotebook(
        const QString & guid) const;

    [[nodiscard]] bool removeLinkedNotebook(const QString & guid);

    void setExpungedLinkedNotebookGuid(const QString & guid);

    [[nodiscard]] bool containsExpungedLinkedNotebookGuid(
        const QString & guid) const;

    [[nodiscard]] bool removeExpungedLinkedNotebookGuid(const QString & guid);

    // Other
    [[nodiscard]] quint32 maxNumSavedSearches() const;
    void setMaxNumSavedSearches(const quint32 maxNumSavedSearches);

    [[nodiscard]] quint32 maxNumTags() const;
    void setMaxNumTags(const quint32 maxNumTags);

    [[nodiscard]] quint32 maxNumNotebooks() const;
    void setMaxNumNotebooks(const quint32 maxNumNotebooks);

    [[nodiscard]] quint32 maxNumNotes() const;
    void setMaxNumNotes(const quint32 maxNumNotes);

    [[nodiscard]] quint64 maxNoteSize() const;
    void setMaxNoteSize(const quint64 maxNoteSize);

    [[nodiscard]] quint32 maxNumResourcesPerNote() const;
    void setMaxNumResourcesPerNote(const quint32 maxNumResourcesPerNote);

    [[nodiscard]] quint32 maxNumTagsPerNote() const;
    void setMaxNumTagsPerNote(const quint32 maxNumTagsPerNote);

    [[nodiscard]] quint64 maxResourceSize() const;
    void setMaxResourceSize(const quint64 maxResourceSize);

    [[nodiscard]] QString linkedNotebookAuthTokenForNotebook(
        const QString & notebookGuid) const;

    void setLinkedNotebookAuthTokenForNotebook(
        const QString & notebookGuid, const QString & linkedNotebookAuthToken);

    [[nodiscard]] qevercloud::SyncState syncState() const;
    void setSyncState(const qevercloud::SyncState & syncState);

    [[nodiscard]] const qevercloud::SyncState * findLinkedNotebookSyncState(
        const QString & linkedNotebookOwner) const;

    void setLinkedNotebookSyncState(
        const QString & linkedNotebookOwner,
        const qevercloud::SyncState & syncState);

    [[nodiscard]] bool removeLinkedNotebookSyncState(
        const QString & linkedNotebookOwner);

    [[nodiscard]] const QString & authToken() const;
    void setAuthToken(const QString & authToken);

    [[nodiscard]] QString linkedNotebookAuthToken(
        const QString & linkedNotebookOwner) const;

    void setLinkedNotebookAuthToken(
        const QString & linkedNotebookOwner,
        const QString & linkedNotebookAuthToken);

    [[nodiscard]] bool removeLinkedNotebookAuthToken(
        const QString & linkedNotebookOwner);

    [[nodiscard]] qint32 currentMaxUsn(
        const QString & linkedNotebookGuid = {}) const;

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

    [[nodiscard]] APIRateLimitsTrigger apiRateLimitsTrigger() const;

    void setAPIRateLimitsExceedingTrigger(const APIRateLimitsTrigger trigger);

    void considerAllExistingDataItemsSentBeforeRateLimitBreach();

    [[nodiscard]] qint32
        smallestUsnOfNotCompletelySentDataItemBeforeRateLimitBreach(
            const QString & linkedNotebookGuid = {}) const;

    [[nodiscard]] qint32 maxUsnBeforeAPIRateLimitsExceeding(
        const QString & linkedNotebookGuid = {}) const;

public:
    // INoteStore interface
    [[nodiscard]] INoteStore * create() const override;

    [[nodiscard]] QString noteStoreUrl() const override;

    void setNoteStoreUrl(QString noteStoreUrl) override;

    void setAuthData(
        QString authenticationToken, QList<QNetworkCookie> cookies) override;

    void stop() override;

    [[nodiscard]] qint32 createNotebook(
        qevercloud::Notebook & notebook, ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    [[nodiscard]] qint32 updateNotebook(
        qevercloud::Notebook & notebook, ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    [[nodiscard]] qint32 createNote(
        qevercloud::Note & note, ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    [[nodiscard]] qint32 updateNote(
        qevercloud::Note & note, ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    [[nodiscard]] qint32 createTag(
        qevercloud::Tag & tag, ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    [[nodiscard]] qint32 updateTag(
        qevercloud::Tag & tag, ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    [[nodiscard]] qint32 createSavedSearch(
        qevercloud::SavedSearch & savedSearch, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    [[nodiscard]] qint32 updateSavedSearch(
        qevercloud::SavedSearch & savedSearch, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    [[nodiscard]] qint32 getSyncState(
        qevercloud::SyncState & syncState, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    [[nodiscard]] qint32 getSyncChunk(
        const qint32 afterUSN, const qint32 maxEntries,
        const qevercloud::SyncChunkFilter & filter,
        qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    [[nodiscard]] qint32 getLinkedNotebookSyncState(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const QString & authToken, qevercloud::SyncState & syncState,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) override;

    [[nodiscard]] qint32 getLinkedNotebookSyncChunk(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const qint32 afterUSN, const qint32 maxEntries,
        const QString & linkedNotebookAuthToken, const bool fullSyncOnly,
        qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    [[nodiscard]] qint32 getNote(
        const bool withContent, const bool withResourcesData,
        const bool withResourcesRecognition,
        const bool withResourcesAlternateData, qevercloud::Note & note,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) override;

    [[nodiscard]] bool getNoteAsync(
        const bool withContent, const bool withResourcesData,
        const bool withResourcesRecognition,
        const bool withResourceAlternateData, const bool withSharedNotes,
        const bool withNoteAppDataValues, const bool withResourceAppDataValues,
        const bool withNoteLimits, const QString & noteGuid,
        const QString & authToken, ErrorString & errorDescription) override;

    [[nodiscard]] qint32 getResource(
        const bool withDataBody, const bool withRecognitionDataBody,
        const bool withAlternateDataBody, const bool withAttributes,
        const QString & authToken, qevercloud::Resource & resource,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) override;

    [[nodiscard]] bool getResourceAsync(
        const bool withDataBody, const bool withRecognitionDataBody,
        const bool withAlternateDataBody, const bool withAttributes,
        const QString & resourceGuid, const QString & authToken,
        ErrorString & errorDescription) override;

    [[nodiscard]] qint32 authenticateToSharedNotebook(
        const QString & shareKey, qevercloud::AuthenticationResult & authResult,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) override;

private:
    void timerEvent(QTimerEvent * event) override;

private:
    [[nodiscard]] qint32 checkNotebookFields(
        const qevercloud::Notebook & notebook,
        ErrorString & errorDescription) const;

    enum class CheckNoteFieldsPurpose
    {
        CreateNote = 0,
        UpdateNote
    };

    [[nodiscard]] qint32 checkNoteFields(
        const qevercloud::Note & note, const CheckNoteFieldsPurpose purpose,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 checkResourceFields(
        const qevercloud::Resource & resource,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 checkTagFields(
        const qevercloud::Tag & tag, ErrorString & errorDescription) const;

    [[nodiscard]] qint32 checkSavedSearchFields(
        const qevercloud::SavedSearch & savedSearch,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 checkLinkedNotebookFields(
        const qevercloud::LinkedNotebook & linkedNotebook,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 checkAppData(
        const qevercloud::LazyMap & appData,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 checkAppDataKey(
        const QString & key, const QRegExp & keyRegExp,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 checkLinkedNotebookAuthToken(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const QString & linkedNotebookAuthToken,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 checkLinkedNotebookAuthTokenForNotebook(
        const QString & notebookGuid, const QString & linkedNotebookAuthToken,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 checkLinkedNotebookAuthTokenForTag(
        const qevercloud::Tag & tag, const QString & linkedNotebookAuthToken,
        ErrorString & errorDescription) const;

    /**
     * Generates next presumably unoccupied name by appending _ and number to
     * the end of the original name or increasing the number if it's already
     * there
     */
    [[nodiscard]] QString nextName(const QString & name) const;

    [[nodiscard]] qint32 getSyncChunkImpl(
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
    [[nodiscard]] ConstIterator advanceIterator(
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

    struct SavedSearchDataExtractor
    {
        [[nodiscard]] static QString name(
            const qevercloud::SavedSearch & search)
        {
            const auto & name = search.name();
            if (name) {
                return *name;
            }
            else {
                return {};
            }
        }

        [[nodiscard]] static QString nameUpper(
            const qevercloud::SavedSearch & search)
        {
            return name(search).toUpper();
        }

        [[nodiscard]] static QString guid(
            const qevercloud::SavedSearch & search)
        {
            const auto & guid = search.guid();
            if (guid) {
                return *guid;
            }
            else {
                return {};
            }
        }

        [[nodiscard]] static qint32 updateSequenceNumber(
            const qevercloud::SavedSearch & search)
        {
            const auto & usn = search.updateSequenceNum();
            if (usn) {
                return *usn;
            }
            else {
                return 0;
            }
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

    struct TagDataExtractor
    {
        [[nodiscard]] static QString name(const qevercloud::Tag & tag)
        {
            const auto & name = tag.name();
            if (name) {
                return *name;
            }
            else {
                return {};
            }
        }

        [[nodiscard]] static QString nameUpper(const qevercloud::Tag & tag)
        {
            return name(tag).toUpper();
        }

        [[nodiscard]] static QString guid(const qevercloud::Tag & tag)
        {
            const auto & guid = tag.guid();
            if (guid) {
                return *guid;
            }
            else {
                return {};
            }
        }

        [[nodiscard]] static qint32 updateSequenceNumber(
            const qevercloud::Tag & tag)
        {
            const auto & usn = tag.updateSequenceNum();
            if (usn) {
                return *usn;
            }
            else {
                return 0;
            }
        }

        [[nodiscard]] static QString parentTagGuid(const qevercloud::Tag & tag)
        {
            const auto & parentTagGuid = tag.parentGuid();
            if (parentTagGuid) {
                return *parentTagGuid;
            }
            else {
                return {};
            }
        }

        [[nodiscard]] static QString linkedNotebookGuid(
            const qevercloud::Tag & tag)
        {
            const auto & linkedNotebookGuid = tag.linkedNotebookGuid();
            if (linkedNotebookGuid) {
                return *linkedNotebookGuid;
            }
            else {
                return {};
            }
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

    // Notebook store
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
            const auto & name = notebook.name();
            if (name) {
                return *name;
            }
            else {
                return {};
            }
        }

        [[nodiscard]] static QString nameUpper(
            const qevercloud::Notebook & notebook)
        {
            return name(notebook).toUpper();
        }

        [[nodiscard]] static QString guid(const qevercloud::Notebook & notebook)
        {
            const auto & guid = notebook.guid();
            if (guid) {
                return *guid;
            }
            else {
                return {};
            }
        }

        [[nodiscard]] static qint32 updateSequenceNumber(
            const qevercloud::Notebook & notebook)
        {
            const auto & usn = notebook.updateSequenceNum();
            if (usn) {
                return *usn;
            }
            else {
                return 0;
            }
        }

        [[nodiscard]] static QString linkedNotebookGuid(
            const qevercloud::Notebook & notebook)
        {
            const auto & linkedNotebookGuid = notebook.linkedNotebookGuid();
            if (linkedNotebookGuid) {
                return *linkedNotebookGuid;
            }
            else {
                return {};
            }
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

    // Note store
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
        [[nodiscard]] static QString guid(const qevercloud::Note & note)
        {
            const auto & guid = note.guid();
            if (guid) {
                return *guid;
            }
            else {
                return {};
            }
        }

        [[nodiscard]] static qint32 updateSequenceNumber(
            const qevercloud::Note & note)
        {
            const auto & usn = note.updateSequenceNum();
            if (usn) {
                return *usn;
            }
            else {
                return 0;
            }
        }

        [[nodiscard]] static QString notebookGuid(
            const qevercloud::Note & note)
        {
            const auto & notebookGuid = note.notebookGuid();
            if (notebookGuid) {
                return *notebookGuid;
            }
            else {
                return {};
            }
        }

        [[nodiscard]] static QString conflictSourceNoteGuid(
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

    // Resource store
    struct ResourceByGuid
    {};

    struct ResourceByUSN
    {};

    struct ResourceByNoteGuid
    {};

    struct ResourceDataExtractor
    {
        [[nodiscard]] static QString guid(const qevercloud::Resource & resource)
        {
            const auto & guid = resource.guid();
            if (guid) {
                return *guid;
            }
            else {
                return {};
            }
        }

        [[nodiscard]] static qint32 updateSequenceNumber(
            const qevercloud::Resource & resource)
        {
            const auto & usn = resource.updateSequenceNum();
            if (usn) {
                return *usn;
            }
            else {
                return 0;
            }
        }

        [[nodiscard]] static QString noteGuid(
            const qevercloud::Resource & resource)
        {
            const auto & noteGuid = resource.noteGuid();
            if (noteGuid) {
                return *noteGuid;
            }
            else {
                return {};
            }
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

    struct LinkedNotebookDataExtractor
    {
        [[nodiscard]] static QString guid(
            const qevercloud::LinkedNotebook & linkedNotebook)
        {
            const auto & guid = linkedNotebook.guid();
            if (guid) {
                return *guid;
            }
            else {
                return {};
            }
        }

        [[nodiscard]] static QString shardId(
            const qevercloud::LinkedNotebook & linkedNotebook)
        {
            const auto & shardId = linkedNotebook.shardId();
            if (shardId) {
                return *shardId;
            }
            else {
                return {};
            }
        }

        [[nodiscard]] static QString uri(
            const qevercloud::LinkedNotebook & linkedNotebook)
        {
            const auto & uri = linkedNotebook.uri();
            if (uri) {
                return *uri;
            }
            else {
                return {};
            }
        }

        [[nodiscard]] static QString username(
            const qevercloud::LinkedNotebook & linkedNotebook)
        {
            const auto & username = linkedNotebook.username();
            if (username) {
                return *username;
            }
            else {
                return {};
            }
        }

        [[nodiscard]] static qint32 updateSequenceNumber(
            const qevercloud::LinkedNotebook & linkedNotebook)
        {
            const auto & usn = linkedNotebook.updateSequenceNum();
            if (usn) {
                return *usn;
            }
            else {
                return 0;
            }
        }

        [[nodiscard]] static QString sharedNotebookGlobalId(
            const qevercloud::LinkedNotebook & linkedNotebook)
        {
            const auto & sharedNotebookGlobalId =
                linkedNotebook.sharedNotebookGlobalId();

            if (sharedNotebookGlobalId) {
                return *sharedNotebookGlobalId;
            }
            else {
                return {};
            }
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

    template <class T>
    class CompareByUSN
    {
    public:
        [[nodiscard]] bool operator()(const qint32 usn, const T & item) const
        {
            return usn < item.updateSequenceNum().value();
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
    [[nodiscard]] NoteDataByUSN::const_iterator nextNoteByUsnIterator(
        NoteDataByUSN::const_iterator it,
        const QString & targetLinkedNotebookGuid = {}) const;

    [[nodiscard]] ResourceDataByUSN::const_iterator nextResourceByUsnIterator(
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
