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

#include <quentier_private/synchronization/INoteStore.h>

namespace quentier {

INoteStore::INoteStore(const QSharedPointer<qevercloud::NoteStore> & pQecNoteStore, QObject * parent) :
    QObject(parent),
    m_pQecNoteStore(pQecNoteStore)
{}

INoteStore::~INoteStore()
{}

QSharedPointer<qevercloud::NoteStore> INoteStore::getQecNoteStore()
{
    return m_pQecNoteStore;
}

void INoteStore::setQecNoteStore(const QSharedPointer<qevercloud::NoteStore> & pQecNoteStore)
{
    m_pQecNoteStore = pQecNoteStore;
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
