#ifndef LIB_QUENTIER_TYPES_RESOURCE_WRAPPER_H
#define LIB_QUENTIER_TYPES_RESOURCE_WRAPPER_H

#include "IResource.h"
#include <QSharedDataPointer>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(ResourceWrapperData)

/**
 * @brief The ResourceWrapper class creates and manages its own instance of
 * qevercloud::Resource object
 */
class QUENTIER_EXPORT ResourceWrapper: public IResource
{
public:
    ResourceWrapper();
    ResourceWrapper(const IResource & other);
    ResourceWrapper(const ResourceWrapper & other);
    ResourceWrapper(ResourceWrapper && other);
    ResourceWrapper(qevercloud::Resource && other);
    ResourceWrapper(const qevercloud::Resource & other);
    ResourceWrapper & operator=(const ResourceWrapper & other);
    ResourceWrapper & operator=(ResourceWrapper && other);
    virtual ~ResourceWrapper();

    virtual QTextStream & print(QTextStream & strm) const Q_DECL_OVERRIDE;

    friend class Note;

private:
    virtual const qevercloud::Resource & GetEnResource() const Q_DECL_OVERRIDE;
    virtual qevercloud::Resource & GetEnResource() Q_DECL_OVERRIDE;

    QSharedDataPointer<ResourceWrapperData> d;
};

} // namespace quentier

Q_DECLARE_METATYPE(quentier::ResourceWrapper)

#endif // LIB_QUENTIER_TYPES_RESOURCE_WRAPPER_H
