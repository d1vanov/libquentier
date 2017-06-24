#include "SynchronizationShared.h"

namespace quentier {

LinkedNotebookAuthData::LinkedNotebookAuthData() :
    m_guid(),
    m_shardId(),
    m_sharedNotebookGlobalId()
{}

LinkedNotebookAuthData::LinkedNotebookAuthData(const QString & guid,
                                               const QString & shardId,
                                               const QString & sharedNotebookGlobalId,
                                               const QString & noteStoreUrl) :
    m_guid(guid),
    m_shardId(shardId),
    m_sharedNotebookGlobalId(sharedNotebookGlobalId),
    m_noteStoreUrl(noteStoreUrl)
{}

QTextStream & LinkedNotebookAuthData::print(QTextStream & strm) const
{
    strm << QStringLiteral("LinkedNotebookAuthData: {\n")
         << QStringLiteral("    guid = ") << m_guid
         << QStringLiteral("    shard id = ") << m_shardId
         << QStringLiteral("    shared notebook global id = ") << m_sharedNotebookGlobalId
         << QStringLiteral("    note store url = ") << m_noteStoreUrl
         << QStringLiteral("};\n");

    return strm;
}

} // namespace quentier
