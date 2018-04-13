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
#include <boost/multi_index/global_fun.hpp>
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

    // Other
    void triggerRateLimitReachOnNextCall();

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
    void setLinkedNotebookAuthTokenForNotebook(const QString & notebookGuid, const QString & linkedNotebookAuthToken);

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
    virtual void timerEvent(QTimerEvent * event) Q_DECL_OVERRIDE;

private:
    qint32 currentMaxUsn() const;
    qint32 checkNotebookFields(const Notebook & notebook, ErrorString & errorDescription) const;

    struct CheckNoteFieldsPurpose
    {
        enum type
        {
            CreateNote = 0,
            UpdateNote
        };
    };

    qint32 checkNoteFields(const Note & note, const CheckNoteFieldsPurpose::type purpose, ErrorString & errorDescription) const;
    qint32 checkResourceFields(const Resource & resource, ErrorString & errorDescription) const;

    qint32 checkAppData(const qevercloud::LazyMap & appData, ErrorString & errorDescription) const;
    qint32 checkAppDataKey(const QString & key, const QRegExp & keyRegExp, ErrorString & errorDescription) const;

    qint32 checkLinkedNotebookAuthToken(const QString & notebookGuid, const QString & linkedNotebookAuthToken,
                                        ErrorString & errorDescription) const;

private:
    // Saved searches store
    struct SavedSearchByGuid{};
    struct SavedSearchByUSN{};
    struct SavedSearchByNameUpper{};

    struct SavedSearchNameUpperExtractor
    {
        static QString nameUpper(const SavedSearch & search)
        {
            if (!search.hasName()) {
                return QString();
            }

            return search.name().toUpper();
        }
    };

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
            >,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<SavedSearchByNameUpper>,
                boost::multi_index::global_fun<const SavedSearch&,QString,&SavedSearchNameUpperExtractor::nameUpper>
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

    struct TagNameUpperExtractor
    {
        static QString nameUpper(const Tag & tag)
        {
            if (!tag.hasName()) {
                return QString();
            }

            return tag.name().toUpper();
        }
    };

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
            >,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<TagByNameUpper>,
                boost::multi_index::global_fun<const Tag&,QString,&TagNameUpperExtractor::nameUpper>
            >
        >
    > TagData;

    typedef TagData::index<TagByGuid>::type TagDataByGuid;
    typedef TagData::index<TagByUSN>::type TagDataByUSN;
    typedef TagData::index<TagByNameUpper>::type TagDataByNameUpper;

    // Notebook store
    struct NotebookByGuid{};
    struct NotebookByUSN{};
    struct NotebookByNameUpper{};

    struct NotebookNameUpperExtractor
    {
        static QString nameUpper(const Notebook & notebook)
        {
            if (!notebook.hasName()) {
                return QString();
            }

            return notebook.name().toUpper();
        }
    };

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
            >,
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<NotebookByNameUpper>,
                boost::multi_index::global_fun<const Notebook&,QString,&NotebookNameUpperExtractor::nameUpper>
            >
        >
    > NotebookData;

    typedef NotebookData::index<NotebookByGuid>::type NotebookDataByGuid;
    typedef NotebookData::index<NotebookByUSN>::type NotebookDataByUSN;
    typedef NotebookData::index<NotebookByNameUpper>::type NotebookDataByNameUpper;

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

    bool                m_shouldTriggerRateLimitReachOnNextCall;

    QSet<int>           m_getNoteAsyncDelayTimerIds;
    QSet<int>           m_getResourceAsyncDelayTimerIds;

    quint32             m_maxNumSavedSearches;
    quint32             m_maxNumTags;
    quint32             m_maxNumNotebooks;
    quint32             m_maxNumNotes;

    quint64             m_maxNoteSize;
    quint32             m_maxNumResourcesPerNote;
    quint32             m_maxNumTagsPerNote;
    quint64             m_maxResourceSize;

    QHash<QString,QString>  m_linkedNotebookAuthTokensByNotebookGuid;
};

} // namespace quentier

inline size_t hash_value(const QString & str) { return qHash(str); }

#endif // LIB_QUENTIER_TESTS_FAKE_NOTE_STORE_H
