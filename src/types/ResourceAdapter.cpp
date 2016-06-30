#include <quentier/types/ResourceAdapter.h>
#include <quentier/exception/ResourceAdapterAccessException.h>
#include <quentier/utility/QuentierCheckPtr.h>

namespace quentier {

ResourceAdapter::ResourceAdapter(qevercloud::Resource & externalEnResource) :
    IResource(),
    m_pEnResource(&externalEnResource),
    m_isConst(false)
{}

ResourceAdapter::ResourceAdapter(const qevercloud::Resource & externalEnResource) :
    IResource(),
    m_pEnResource(const_cast<qevercloud::Resource*>(&externalEnResource)),
    m_isConst(true)
{}

ResourceAdapter::ResourceAdapter(const ResourceAdapter & other) :
    IResource(other),
    m_pEnResource(other.m_pEnResource),
    m_isConst(other.m_isConst)
{}

ResourceAdapter::ResourceAdapter(ResourceAdapter && other) :
    IResource(std::move(other)),
    m_pEnResource(std::move(other.m_pEnResource)),
    m_isConst(std::move(other.m_isConst))
{}

ResourceAdapter & ResourceAdapter::operator=(const ResourceAdapter & other)
{
    if (this != &other) {
        IResource::operator=(other);
        m_pEnResource = other.m_pEnResource;
        m_isConst = other.m_isConst;
    }

    return *this;
}

ResourceAdapter::~ResourceAdapter()
{}

const qevercloud::Resource & ResourceAdapter::GetEnResource() const
{
    QUENTIER_CHECK_PTR(m_pEnResource, "Null pointer to external resource in ResourceAdapter");
    return static_cast<const qevercloud::Resource&>(*m_pEnResource);
}

qevercloud::Resource & ResourceAdapter::GetEnResource()
{
    if (m_isConst) {
        throw ResourceAdapterAccessException("Attempt to access non-const reference "
                                             "to resource from constant ResourceAdapter");
    }

    QUENTIER_CHECK_PTR(m_pEnResource, "Null pointer to external resource in ResourceAdapter");
    return static_cast<qevercloud::Resource&>(*m_pEnResource);
}

QTextStream & ResourceAdapter::print(QTextStream & strm) const
{
    strm << "ResourceAdapter { \n";
    IResource::print(strm);
    strm << "} \n";

    return strm;
}

} // namespace quentier
