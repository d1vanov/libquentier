#include "SavedSearchData.h"
#include <quentier/types/SavedSearch.h>
#include <quentier/utility/Utility.h>

namespace quentier {

SavedSearchData::SavedSearchData() :
    FavoritableDataElementData(),
    m_qecSearch()
{
}

SavedSearchData::SavedSearchData(const SavedSearchData & other) :
    FavoritableDataElementData(other),
    m_qecSearch(other.m_qecSearch)
{}

SavedSearchData::SavedSearchData(SavedSearchData && other) :
    FavoritableDataElementData(std::move(other)),
    m_qecSearch(std::move(other.m_qecSearch))
{}

SavedSearchData::SavedSearchData(const qevercloud::SavedSearch & other) :
    FavoritableDataElementData(),
    m_qecSearch(other)
{}

SavedSearchData::SavedSearchData(qevercloud::SavedSearch && other) :
    FavoritableDataElementData(),
    m_qecSearch(std::move(other))
{}

SavedSearchData::~SavedSearchData()
{}

void SavedSearchData::clear()
{
    m_qecSearch = qevercloud::SavedSearch();
}

bool SavedSearchData::checkParameters(QString &errorDescription) const
{
    if (m_qecSearch.guid.isSet() && !checkGuid(m_qecSearch.guid.ref())) {
        errorDescription = QT_TR_NOOP("Saved search's guid is invalid: ") + m_qecSearch.guid;
        return false;
    }

    if (m_qecSearch.name.isSet() && !SavedSearch::validateName(m_qecSearch.name, &errorDescription)) {
        return false;
    }

    if (m_qecSearch.updateSequenceNum.isSet() && !checkUpdateSequenceNumber(m_qecSearch.updateSequenceNum)) {
        errorDescription = QT_TR_NOOP("Saved search's update sequence number is invalid: ");
        errorDescription.append(QString::number(m_qecSearch.updateSequenceNum));
        return false;
    }

    if (m_qecSearch.query.isSet())
    {
        const QString & query = m_qecSearch.query;
        int querySize = query.size();

        if ( (querySize < qevercloud::EDAM_SEARCH_QUERY_LEN_MIN) ||
             (querySize > qevercloud::EDAM_SEARCH_QUERY_LEN_MAX) )
        {
            errorDescription = QT_TR_NOOP("Saved search's query exceeds allowed size: ") + query;
            return false;
        }
    }

    if (m_qecSearch.format.isSet() && (static_cast<qevercloud::QueryFormat::type>(m_qecSearch.format) != qevercloud::QueryFormat::USER)) {
        errorDescription = QT_TR_NOOP("Saved search has unsupported query format");
        return false;
    }

    return true;
}

bool SavedSearchData::operator==(const SavedSearchData & other) const
{
    return (m_qecSearch == other.m_qecSearch) &&
           (m_isDirty == other.m_isDirty) &&
           (m_isLocal == other.m_isLocal) &&
           (m_isFavorited == other.m_isFavorited);
}

bool SavedSearchData::operator!=(const SavedSearchData & other) const
{
    return !(*this == other);
}

} // namespace quentier
