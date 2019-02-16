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

#include <quentier_private/synchronization/IUserStore.h>
#include <quentier/types/User.h>

namespace quentier {

IUserStore::IUserStore(const QSharedPointer<qevercloud::UserStore> & pQecUserStore) :
    m_pQecUserStore(pQecUserStore)
{}

QSharedPointer<qevercloud::UserStore> IUserStore::getQecUserStore() const
{
    return m_pQecUserStore;
}

void IUserStore::setQecUserStore(
    const QSharedPointer<qevercloud::UserStore> & pQecUserStore)
{
    m_pQecUserStore = pQecUserStore;
}

QString IUserStore::authenticationToken() const
{
    if (!m_pQecUserStore.isNull()) {
        return m_pQecUserStore->authenticationToken();
    }

    return QString();
}

void IUserStore::setAuthenticationToken(const QString & authToken)
{
    if (!m_pQecUserStore.isNull()) {
        m_pQecUserStore->setAuthenticationToken(authToken);
    }
}

} // namespace quentier
