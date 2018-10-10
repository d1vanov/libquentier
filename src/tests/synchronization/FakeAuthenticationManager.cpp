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

#include "FakeAuthenticationManager.h"
#include <quentier/utility/UidGenerator.h>
#include <QDateTime>

namespace quentier {

FakeAuthenticationManager::FakeAuthenticationManager(QObject * parent) :
    IAuthenticationManager(parent),
    m_userId(1),
    m_authToken(),
    m_failNextRequest(false)
{}

FakeAuthenticationManager::~FakeAuthenticationManager()
{}

const QString & FakeAuthenticationManager::authToken() const
{
    return m_authToken;
}

void FakeAuthenticationManager::setAuthToken(const QString & authToken)
{
    m_authToken = authToken;
}

qevercloud::UserID FakeAuthenticationManager::userId() const
{
    return m_userId;
}

void FakeAuthenticationManager::setUserId(const qevercloud::UserID userId)
{
    m_userId = userId;
}

void FakeAuthenticationManager::failNextRequest()
{
    m_failNextRequest = true;
}

void FakeAuthenticationManager::onAuthenticationRequest()
{
    if (m_failNextRequest) {
        m_failNextRequest = false;
        Q_EMIT sendAuthenticationResult(false, m_userId, QString(), qevercloud::Timestamp(0),
                                        QString(), QString(), QString(), ErrorString("Artificial error"));
        return;
    }

    Q_EMIT sendAuthenticationResult(true, m_userId, m_authToken,
                                    qevercloud::Timestamp(QDateTime::currentDateTime().addYears(1).toMSecsSinceEpoch()),
                                    UidGenerator::Generate(), QStringLiteral("note_store_url"), QStringLiteral("web_api_url_prefix"),
                                    ErrorString());
}

} // namespace quentier
