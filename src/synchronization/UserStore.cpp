/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/User.h>

#define USER_STORE_REQUEST_TIMEOUT_MSEC (-1)

namespace quentier {

UserStore::UserStore(QString evernoteHost)
{
    m_pUserStore.reset(qevercloud::newUserStore(std::move(evernoteHost)));
}

void UserStore::setAuthData(
    QString authenticationToken, QList<QNetworkCookie> cookies)
{
    m_authenticationToken = std::move(authenticationToken);
    m_cookies = std::move(cookies);
}

bool UserStore::checkVersion(
    const QString & clientName, qint16 edamVersionMajor,
    qint16 edamVersionMinor, ErrorString & errorDescription)
{
    try {
        auto ctx = qevercloud::newRequestContext(
            m_authenticationToken, USER_STORE_REQUEST_TIMEOUT_MSEC,
            qevercloud::DEFAULT_REQUEST_TIMEOUT_EXPONENTIAL_INCREASE,
            qevercloud::DEFAULT_MAX_REQUEST_TIMEOUT_MSEC,
            qevercloud::DEFAULT_MAX_REQUEST_RETRY_COUNT, m_cookies);

        return m_pUserStore->checkVersion(
            clientName, edamVersionMajor, edamVersionMinor, ctx);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    return false;
}

qint32 UserStore::getUser(
    User & user, ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    try {
        auto ctx = qevercloud::newRequestContext(
            m_authenticationToken, USER_STORE_REQUEST_TIMEOUT_MSEC,
            qevercloud::DEFAULT_REQUEST_TIMEOUT_EXPONENTIAL_INCREASE,
            qevercloud::DEFAULT_MAX_REQUEST_TIMEOUT_MSEC,
            qevercloud::DEFAULT_MAX_REQUEST_RETRY_COUNT, m_cookies);

        user.qevercloudUser() = m_pUserStore->getUser(ctx);
        return 0;
    }
    catch (const qevercloud::EDAMUserException & userException) {
        return processEdamUserException(userException, errorDescription);
    }
    catch (const qevercloud::EDAMSystemException & systemException) {
        return processEdamSystemException(
            systemException, errorDescription, rateLimitSeconds);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
}

qint32 UserStore::getAccountLimits(
    const qevercloud::ServiceLevel serviceLevel,
    qevercloud::AccountLimits & limits, ErrorString & errorDescription,
    qint32 & rateLimitSeconds)
{
    try {
        auto ctx = qevercloud::newRequestContext(
            m_authenticationToken, USER_STORE_REQUEST_TIMEOUT_MSEC,
            qevercloud::DEFAULT_REQUEST_TIMEOUT_EXPONENTIAL_INCREASE,
            qevercloud::DEFAULT_MAX_REQUEST_TIMEOUT_MSEC,
            qevercloud::DEFAULT_MAX_REQUEST_RETRY_COUNT, m_cookies);

        limits = m_pUserStore->getAccountLimits(serviceLevel, ctx);
        return 0;
    }
    catch (const qevercloud::EDAMUserException & userException) {
        return processEdamUserException(userException, errorDescription);
    }
    catch (const qevercloud::EDAMSystemException & systemException) {
        return processEdamSystemException(
            systemException, errorDescription, rateLimitSeconds);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
}

qint32 UserStore::processEdamUserException(
    const qevercloud::EDAMUserException & userException,
    ErrorString & errorDescription) const
{
    switch (userException.errorCode) {
    case qevercloud::EDAMErrorCode::BAD_DATA_FORMAT:
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("UserStore", "BAD_DATA_FORMAT exception"));
        break;
    case qevercloud::EDAMErrorCode::INTERNAL_ERROR:
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("UserStore", "INTERNAL_ERROR exception"));
        break;
    case qevercloud::EDAMErrorCode::TAKEN_DOWN:
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("UserStore", "TAKEN_DOWN exception"));
        break;
    case qevercloud::EDAMErrorCode::INVALID_AUTH:
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("UserStore", "INVALID_AUTH exception"));
        break;
    case qevercloud::EDAMErrorCode::AUTH_EXPIRED:
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("UserStore", "AUTH_EXPIRED exception"));
        break;
    case qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED:
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("UserStore", "RATE_LIMIT_REACHED exception"));
        break;
    default:
        errorDescription.setBase(QT_TRANSLATE_NOOP("UserStore", "Error"));

        errorDescription.details() = QStringLiteral("error code = ");
        errorDescription.details() += ToString(userException.errorCode);
        break;
    }

    const auto exceptionData = userException.exceptionData();

    if (userException.parameter.isSet()) {
        errorDescription.details() += QStringLiteral(", parameter: ");
        errorDescription.details() += userException.parameter.ref();
    }

    if (exceptionData && !exceptionData->errorMessage.isEmpty()) {
        errorDescription.details() += QStringLiteral(", message: ");
        errorDescription.details() += exceptionData->errorMessage;
    }

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<int>(userException.errorCode);
}

qint32 UserStore::processEdamSystemException(
    const qevercloud::EDAMSystemException & systemException,
    ErrorString & errorDescription, qint32 & rateLimitSeconds) const
{
    rateLimitSeconds = -1;

    if (systemException.errorCode ==
        qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED) {
        if (!systemException.rateLimitDuration.isSet()) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "UserStore",
                "Evernote API rate limit exceeded but no rate limit duration "
                "is available"));
        }
        else {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "UserStore", "Evernote API rate limit exceeded, retry in"));

            errorDescription.details() =
                QString::number(systemException.rateLimitDuration.ref());

            errorDescription.details() += QStringLiteral(" sec");
            rateLimitSeconds = systemException.rateLimitDuration.ref();
        }
    }
    else {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "UserStore", "Caught EDAM system exception, error code "));

        errorDescription.details() += QStringLiteral("error code = ");
        errorDescription.details() += ToString(systemException.errorCode);

        if (systemException.message.isSet() &&
            !systemException.message->isEmpty()) {
            errorDescription.details() += QStringLiteral(", message: ");
            errorDescription.details() += systemException.message.ref();
        }
    }

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<int>(systemException.errorCode);
}

} // namespace quentier
