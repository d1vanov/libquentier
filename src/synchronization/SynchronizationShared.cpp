#include "SynchronizationShared.h"

namespace quentier {

LinkedNotebookAuthData::LinkedNotebookAuthData() :
    m_guid(),
    m_shardId(),
    m_sharedNotebookGlobalId(),
    m_uri(),
    m_noteStoreUrl()
{}

LinkedNotebookAuthData::LinkedNotebookAuthData(const QString & guid,
                                               const QString & shardId,
                                               const QString & sharedNotebookGlobalId,
                                               const QString & uri,
                                               const QString & noteStoreUrl) :
    m_guid(guid),
    m_shardId(shardId),
    m_sharedNotebookGlobalId(sharedNotebookGlobalId),
    m_uri(uri),
    m_noteStoreUrl(noteStoreUrl)
{}

QTextStream & LinkedNotebookAuthData::print(QTextStream & strm) const
{
    strm << QStringLiteral("LinkedNotebookAuthData: {\n")
         << QStringLiteral("    guid = ") << m_guid << QStringLiteral("\n")
         << QStringLiteral("    shard id = ") << m_shardId << QStringLiteral("\n")
         << QStringLiteral("    shared notebook global id = ") << m_sharedNotebookGlobalId << QStringLiteral("\n")
         << QStringLiteral("    uri = ") << m_uri << QStringLiteral("\n")
         << QStringLiteral("    note store url = ") << m_noteStoreUrl << QStringLiteral("\n")
         << QStringLiteral("};\n");

    return strm;
}

} // namespace quentier
