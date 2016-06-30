#include "UserWrapperData.h"

namespace quentier {

UserWrapperData::UserWrapperData() :
    QSharedData(),
    m_qecUser()
{}

UserWrapperData::UserWrapperData(const UserWrapperData & other) :
    QSharedData(other),
    m_qecUser(other.m_qecUser)
{}

UserWrapperData::UserWrapperData(UserWrapperData && other) :
    QSharedData(std::move(other)),
    m_qecUser(std::move(other.m_qecUser))
{}

UserWrapperData::~UserWrapperData()
{}

} // namespace quentier
