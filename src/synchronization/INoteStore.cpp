#include <quentier/synchronization/INoteStore.h>

namespace quentier {

INoteStore::INoteStore(QSharedPointer<qevercloud::NoteStore> pQecNoteStore, QObject * parent) :
    QObject(parent),
    m_pQecNoteStore(pQecNoteStore)
{}

INoteStore::~INoteStore()
{}

QSharedPointer<qevercloud::NoteStore> INoteStore::getQecNoteStore()
{
    return m_pQecNoteStore;
}

QString INoteStore::noteStoreUrl() const
{
    if (!m_pQecNoteStore.isNull()) {
        return m_pQecNoteStore->noteStoreUrl();
    }

    return QString();
}

void INoteStore::setNoteStoreUrl(const QString & noteStoreUrl)
{
    if (!m_pQecNoteStore.isNull()) {
        m_pQecNoteStore->setNoteStoreUrl(noteStoreUrl);
    }
}

QString INoteStore::authenticationToken() const
{
    if (!m_pQecNoteStore.isNull()) {
        return m_pQecNoteStore->authenticationToken();
    }

    return QString();
}

void INoteStore::setAuthenticationToken(const QString & authToken)
{
    if (!m_pQecNoteStore.isNull()) {
        m_pQecNoteStore->setAuthenticationToken(authToken);
    }
}

} // namespace quentier
