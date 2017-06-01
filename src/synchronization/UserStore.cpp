/*
 * Copyright 2016 Dmitry Ivanov
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

#include "UserStore.h"
#include "ExceptionHandlingHelpers.h"
#include <quentier/types/User.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/QuentierCheckPtr.h>

namespace quentier {

UserStore::UserStore(QSharedPointer<qevercloud::UserStore> pQecUserStore) :
    m_pQecUserStore(pQecUserStore)
{
    QUENTIER_CHECK_PTR(m_pQecUserStore)
}

QSharedPointer<qevercloud::UserStore> UserStore::getQecUserStore()
{
    return m_pQecUserStore;
}

QString UserStore::authenticationToken() const
{
    return m_pQecUserStore->authenticationToken();
}

void UserStore::setAuthenticationToken(const QString & authToken)
{
    m_pQecUserStore->setAuthenticationToken(authToken);
}

bool UserStore::checkVersion(const QString & clientName, qint16 edamVersionMajor, qint16 edamVersionMinor,
                             ErrorString & errorDescription)
{
    try
    {
        return m_pQecUserStore->checkVersion(clientName, edamVersionMajor, edamVersionMinor);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    return false;
}

qint32 UserStore::getUser(User & user, ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    try
    {
        user = m_pQecUserStore->getUser(m_pQecUserStore->authenticationToken());
        return 0;
    }
    catch(const qevercloud::EDAMUserException & userException)
    {
        return processEdamUserException(userException, errorDescription);
    }
    catch(const qevercloud::EDAMSystemException & systemException)
    {
        return processEdamSystemException(systemException, errorDescription,
                                          rateLimitSeconds);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    return qevercloud::EDAMErrorCode::UNKNOWN;
}

qint32 UserStore::getAccountLimits(const qevercloud::ServiceLevel::type serviceLevel, qevercloud::AccountLimits & limits,
                                   ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    try
    {
        limits = m_pQecUserStore->getAccountLimits(serviceLevel);
        return 0;
    }
    catch(const qevercloud::EDAMUserException & userException)
    {
        return processEdamUserException(userException, errorDescription);
    }
    catch(const qevercloud::EDAMSystemException & systemException)
    {
        return processEdamSystemException(systemException, errorDescription,
                                          rateLimitSeconds);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    return qevercloud::EDAMErrorCode::UNKNOWN;
}

qint32 UserStore::processEdamUserException(const qevercloud::EDAMUserException & userException, ErrorString & errorDescription) const
{
    switch(userException.errorCode)
    {
    case qevercloud::EDAMErrorCode::BAD_DATA_FORMAT:
        errorDescription.setBase(QT_TR_NOOP("BAD_DATA_FORMAT exception"));
        break;
    case qevercloud::EDAMErrorCode::INTERNAL_ERROR:
        errorDescription.setBase(QT_TR_NOOP("INTERNAL_ERROR exception"));
        break;
    case qevercloud::EDAMErrorCode::TAKEN_DOWN:
        errorDescription.setBase(QT_TR_NOOP("TAKEN_DOWN exception"));
        break;
    case qevercloud::EDAMErrorCode::INVALID_AUTH:
        errorDescription.setBase(QT_TR_NOOP("INVALID_AUTH exception"));
        break;
    case qevercloud::EDAMErrorCode::AUTH_EXPIRED:
        errorDescription.setBase(QT_TR_NOOP("AUTH_EXPIRED exception"));
        break;
    case qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED:
        errorDescription.setBase(QT_TR_NOOP("RATE_LIMIT_REACHED exception"));
        break;
    default:
        errorDescription.setBase(QT_TR_NOOP("Error"));
        errorDescription.details() = QStringLiteral("error code = ");
        errorDescription.details() += QString::number(userException.errorCode);
        break;
    }

    const auto exceptionData = userException.exceptionData();

    if (userException.parameter.isSet()) {
        errorDescription.details() += QStringLiteral(", parameter: ");
        errorDescription.details() += userException.parameter.ref();
    }

    if (!exceptionData.isNull() && !exceptionData->errorMessage.isEmpty()) {
        errorDescription.details() += QStringLiteral(", message: ");
        errorDescription.details() += exceptionData->errorMessage;
    }

    return userException.errorCode;
}

qint32 UserStore::processEdamSystemException(const qevercloud::EDAMSystemException & systemException,
                                             ErrorString & errorDescription, qint32 & rateLimitSeconds) const
{
    rateLimitSeconds = -1;

    if (systemException.errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
    {
        if (!systemException.rateLimitDuration.isSet()) {
            errorDescription.setBase(QT_TR_NOOP("Evernote API rate limit exceeded but "
                                                "no rate limit duration is available"));
        }
        else {
            errorDescription.setBase(QT_TR_NOOP("Evernote API rate limit exceeded, retry in"));
            errorDescription.details() = QString::number(systemException.rateLimitDuration.ref());
            errorDescription.details() += QStringLiteral(" sec");
            rateLimitSeconds = systemException.rateLimitDuration.ref();
        }
    }
    else
    {
        errorDescription.setBase(QT_TR_NOOP("Caught EDAM system exception, error code "));
        errorDescription.details() += QStringLiteral("error code = ");
        errorDescription.details() += ToString(systemException.errorCode);

        if (systemException.message.isSet() && !systemException.message->isEmpty()) {
            errorDescription.details() += QStringLiteral(", message: ");
            errorDescription.details() += systemException.message.ref();
        }
    }

    return systemException.errorCode;
}

} // namespace quentier
