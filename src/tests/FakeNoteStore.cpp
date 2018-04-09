#include "FakeNoteStore.h"

namespace quentier {

FakeNoteStore::FakeNoteStore(QObject * parent) :
    INoteStore(QSharedPointer<qevercloud::NoteStore>(new qevercloud::NoteStore), parent)
{}

QHash<QString,qevercloud::SavedSearch> FakeNoteStore::savedSearches() const
{
    QHash<QString,qevercloud::SavedSearch> result;
    result.reserve(static_cast<int>(m_savedSearches.size()));

    const SavedSearchDataByGuid & savedSearchByGuid = m_savedSearches.get<SavedSearchByGuid>();
    for(auto it = savedSearchByGuid.begin(), end = savedSearchByGuid.end(); it != end; ++it) {
        result[it->guid()] = it->qevercloudSavedSearch();
    }

    return result;
}

bool FakeNoteStore::setSavedSearch(SavedSearch & search, ErrorString & errorDescription)
{
    if (!search.hasGuid()) {
        errorDescription.setBase(QStringLiteral("Can't set saved search without guid"));
        return false;
    }

    qint32 maxUsn = currentMaxUsn();
    ++maxUsn;
    search.setUpdateSequenceNumber(maxUsn);

    // TODO: set appropriate USN to the saved search

    // TODO: implement further
    return true;
}

} // namespace quentier
