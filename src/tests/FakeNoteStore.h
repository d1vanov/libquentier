/*
 * Copyright 2018 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TESTS_FAKE_NOTE_STORE_H
#define LIB_QUENTIER_TESTS_FAKE_NOTE_STORE_H

#include <quentier/synchronization/INoteStore.h>
#include <quentier/types/SavedSearch.h>
#include <quentier/types/Tag.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/Note.h>
#include <quentier/utility/Macros.h>

// NOTE: Workaround a bug in Qt4 which may prevent building with some boost versions
#ifndef Q_MOC_RUN
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/bimap.hpp>
#endif

#include <QSet>
#include <QHash>

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
    bool containsExpungedSavedSearchGuid(const QString & guid);
    bool removeExpungedSavedSearchGuid(const QString & guid);

    // Tags
    QHash<QString,qevercloud::Tag> tags() const;

    bool setTag(Tag & tag, ErrorString & errorDescription);
    const Tag * findTag(const QString & guid) const;
    bool removeTag(const QString & guid);

    void setExpungedTagGuid(const QString & guid);
    bool containsExpungedTagGuid(const QString & guid);
    bool removeExpungedTagGuid(const QString & guid);

    // Notebooks
    QHash<QString,qevercloud::Notebook> notebooks() const;

    bool setNotebook(Notebook & notebook, ErrorString & errorDescription);
    const Notebook * findNotebook(const QString & guid);
    bool removeNotebook(const QString & guid);

    void setExpungedNotebookGuid(const QString & guid);
    bool containsExpungedNotebookGuid(const QString & guid);
    bool removeExpungedNotebookGuid(const QString & guid);

    // Notes
    QHash<QString,qevercloud::Note> notes() const;

    bool setNote(Note & note, ErrorString & errorDescription);
    const Note * findNote(const QString & guid);
    bool removeNote(const QString & guid);

    void setExpungedNoteGuid(const QString & guid);
    bool containsExpungedNoteGuid(const QString & guid);
    bool removeExpungedNoteGuid(const QString & guid);

public:
    // INoteStore interface
    virtual INoteStore * create() const Q_DECL_OVERRIDE;
    virtual void stop() Q_DECL_OVERRIDE;
    virtual qint32 createNotebook(Notebook & notebook, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                                  const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;
    virtual qint32 updateNotebook(Notebook & notebook, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                                  const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;
    virtual qint32 createNote(Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                              const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;
    virtual qint32 updateNote(Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                              const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;
    virtual qint32 createTag(Tag & tag, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                             const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;
    virtual qint32 updateTag(Tag & tag, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                             const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;
    virtual qint32 createSavedSearch(SavedSearch & savedSearch, ErrorString & errorDescription, qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;
    virtual qint32 updateSavedSearch(SavedSearch & savedSearch, ErrorString & errorDescription, qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;
    virtual qint32 getSyncState(qevercloud::SyncState & syncState, ErrorString & errorDescription, qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;
    virtual qint32 getSyncChunk(const qint32 afterUSN, const qint32 maxEntries, const qevercloud::SyncChunkFilter & filter,
                                qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
                                qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;
    virtual qint32 getLinkedNotebookSyncState(const qevercloud::LinkedNotebook & linkedNotebook,
                                              const QString & authToken, qevercloud::SyncState & syncState,
                                              ErrorString & errorDescription, qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;
    virtual qint32 getLinkedNotebookSyncChunk(const qevercloud::LinkedNotebook & linkedNotebook,
                                              const qint32 afterUSN, const qint32 maxEntries,
                                              const QString & linkedNotebookAuthToken, const bool fullSyncOnly,
                                              qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
                                              qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;
    virtual qint32 getNote(const bool withContent, const bool withResourcesData,
                           const bool withResourcesRecognition, const bool withResourceAlternateData,
                           Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;
    virtual bool getNoteAsync(const bool withContent, const bool withResourceData, const bool withResourcesRecognition,
                              const bool withResourceAlternateData, const bool withSharedNotes,
                              const bool withNoteAppDataValues, const bool withResourceAppDataValues,
                              const bool withNoteLimits, const QString & noteGuid,
                              const QString & authToken, ErrorString & errorDescription) Q_DECL_OVERRIDE;
    virtual qint32 getResource(const bool withDataBody, const bool withRecognitionDataBody,
                               const bool withAlternateDataBody, const bool withAttributes,
                               const QString & authToken, Resource & resource, ErrorString & errorDescription,
                               qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;
    virtual bool getResourceAsync(const bool withDataBody, const bool withRecognitionDataBody,
                                  const bool withAlternateDataBody, const bool withAttributes, const QString & resourceGuid,
                                  const QString & authToken, ErrorString & errorDescription) Q_DECL_OVERRIDE;
    virtual qint32 authenticateToSharedNotebook(const QString & shareKey, qevercloud::AuthenticationResult & authResult,
                                                ErrorString & errorDescription, qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;

private:
    qint32 currentMaxUsn() const;

private:
    // Saved searches store
    struct SavedSearchByGuid{};
    struct SavedSearchByUSN{};

    typedef boost::multi_index_container<
        SavedSearch,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<SavedSearchByGuid>,
                boost::multi_index::const_mem_fun<SavedSearch,const QString&,&SavedSearch::guid>
            >,
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<SavedSearchByUSN>,
                boost::multi_index::const_mem_fun<SavedSearch,qint32,&SavedSearch::updateSequenceNumber>
            >
        >
    > SavedSearchData;

    typedef SavedSearchData::index<SavedSearchByGuid>::type SavedSearchDataByGuid;
    typedef SavedSearchData::index<SavedSearchByUSN>::type SavedSearchDataByUSN;

    // Tag store
    struct TagByGuid{};
    struct TagByUSN{};

    typedef boost::multi_index_container<
        Tag,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<TagByGuid>,
                boost::multi_index::const_mem_fun<Tag,const QString&,&Tag::guid>
            >,
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<TagByUSN>,
                boost::multi_index::const_mem_fun<Tag,qint32,&Tag::updateSequenceNumber>
            >
        >
    > TagData;

    typedef TagData::index<TagByGuid>::type TagDataByGuid;
    typedef TagData::index<TagByUSN>::type TagDataByUSN;

    // Notebook store
    struct NotebookByGuid{};
    struct NotebookByUSN{};

    typedef boost::multi_index_container<
        Notebook,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<NotebookByGuid>,
                boost::multi_index::const_mem_fun<Notebook,const QString&,&Notebook::guid>
            >,
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<NotebookByUSN>,
                boost::multi_index::const_mem_fun<Notebook,qint32,&Notebook::updateSequenceNumber>
            >
        >
    > NotebookData;

    typedef NotebookData::index<NotebookByGuid>::type NotebookDataByGuid;
    typedef NotebookData::index<NotebookByUSN>::type NotebookDataByUSN;

    // Note store
    struct NoteByGuid{};
    struct NoteByUSN{};

    typedef boost::multi_index_container<
        Note,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<NoteByGuid>,
                boost::multi_index::const_mem_fun<Note,const QString&,&Note::guid>
            >,
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<NoteByUSN>,
                boost::multi_index::const_mem_fun<Note,qint32,&Note::updateSequenceNumber>
            >
        >
    > NoteData;

    typedef NoteData::index<NoteByGuid>::type NoteDataByGuid;
    typedef NoteData::index<NoteByUSN>::type NoteDataByUSN;

private:
    SavedSearchData     m_savedSearches;
    QSet<QString>       m_expungedSavedSearchGuids;

    TagData             m_tags;
    QSet<QString>       m_expungedTagGuids;

    NotebookData        m_notebooks;
    QSet<QString>       m_expungedNotebookGuids;

    NoteData            m_notes;
    QSet<QString>       m_expungedNoteGuids;
};

} // namespace quentier

#endif // LIB_QUENTIER_TESTS_FAKE_NOTE_STORE_H
