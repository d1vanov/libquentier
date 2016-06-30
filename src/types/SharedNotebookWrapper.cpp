#include "data/SharedNotebookWrapperData.h"
#include <quentier/types/SharedNotebookWrapper.h>

namespace quentier {

SharedNotebookWrapper::SharedNotebookWrapper() :
    ISharedNotebook(),
    d(new SharedNotebookWrapperData)
{}

SharedNotebookWrapper::SharedNotebookWrapper(const SharedNotebookWrapper & other) :
    ISharedNotebook(other),
    d(other.d)
{}

SharedNotebookWrapper::SharedNotebookWrapper(SharedNotebookWrapper && other) :
    ISharedNotebook(),
    d(other.d)
{}

SharedNotebookWrapper::SharedNotebookWrapper(const qevercloud::SharedNotebook & other) :
    ISharedNotebook(),
    d(new SharedNotebookWrapperData(other))
{}

SharedNotebookWrapper::SharedNotebookWrapper(qevercloud::SharedNotebook && other) :
    ISharedNotebook(),
    d(new SharedNotebookWrapperData(std::move(other)))
{}

SharedNotebookWrapper & SharedNotebookWrapper::operator=(const SharedNotebookWrapper & other)
{
    ISharedNotebook::operator=(other);

    if (this != &other) {
        d = other.d;
    }

    return *this;
}

SharedNotebookWrapper & SharedNotebookWrapper::operator=(SharedNotebookWrapper && other)
{
    ISharedNotebook::operator=(std::move(other));

    if (this != &other) {
        d = other.d;
    }

    return *this;
}

SharedNotebookWrapper::~SharedNotebookWrapper()
{}

const qevercloud::SharedNotebook & SharedNotebookWrapper::GetEnSharedNotebook() const
{
    return d->m_qecSharedNotebook;
}

qevercloud::SharedNotebook & SharedNotebookWrapper::GetEnSharedNotebook()
{
    return d->m_qecSharedNotebook;
}

QTextStream & SharedNotebookWrapper::print(QTextStream & strm) const
{
    strm << "SharedNotebookWrapper: { \n";
    ISharedNotebook::print(strm);
    strm << "}; \n";

    return strm;
}

} // namespace quentier
