/*
 * Copyright 2018-2019 Dmitry Ivanov
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

INoteStore::INoteStore(
        const qevercloud::INoteStorePtr & pQecNoteStore,
        QObject * parent) :
    QObject(parent),
    m_pQecNoteStore(pQecNoteStore),
    m_authenticationToken()
{}

INoteStore::~INoteStore()
{}

qevercloud::INoteStorePtr INoteStore::getQecNoteStore()
{
    return m_pQecNoteStore;
}

void INoteStore::setQecNoteStore(
    const qevercloud::INoteStorePtr & pQecNoteStore)
{
    m_pQecNoteStore = pQecNoteStore;
}

QString INoteStore::noteStoreUrl() const
{
    if (m_pQecNoteStore) {
        return m_pQecNoteStore->noteStoreUrl();
    }

    return QString();
}

void INoteStore::setNoteStoreUrl(const QString & noteStoreUrl)
{
    if (m_pQecNoteStore) {
        m_pQecNoteStore->setNoteStoreUrl(noteStoreUrl);
    }
}

QString INoteStore::authenticationToken() const
{
    return m_authenticationToken;
}

void INoteStore::setAuthenticationToken(const QString & authToken)
{
    m_authenticationToken = authToken;
}

} // namespace quentier
