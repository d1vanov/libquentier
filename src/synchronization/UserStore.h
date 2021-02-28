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
#ifndef LIB_QUENTIER_SYNCHRONIZATION_USER_STORE_H
#define LIB_QUENTIER_SYNCHRONIZATION_USER_STORE_H

#include <quentier/synchronization/IUserStore.h>

namespace quentier {

/**
 * @brief The UserStore class in quentier namespace is a wrapper under UserStore
 * from QEverCloud.
 *
 * The main difference from the underlying class is stronger exception safety:
 * most QEverCloud's methods throw exceptions to indicate errors (much like
 * the native Evernote API for supported languages do). Using exceptions along
 * with Qt is not simple and desirable. Therefore, this class' methods
 * simply redirect the requests to methods of QEverCloud's UserStore but catch
 * the "expected" exceptions, "parse" their internal error flags and return
 * the textual representation of the error.
 *
 * libquentier at the moment uses only several methods from those available
 * in QEverCloud's UserStore so only the small subset of original UserStore's
 * API is wrapped at the moment.
 */
class Q_DECL_HIDDEN UserStore final : public IUserStore
{
public:
    UserStore(QString evernoteHost);

    virtual ~UserStore() override = default;

public:
    // IUserStore interface

    virtual void setAuthData(
        QString authenticationToken, QList<QNetworkCookie> cookies) override;

    virtual bool checkVersion(
        const QString & clientName, qint16 edamVersionMajor,
        qint16 edamVersionMinor, ErrorString & errorDescription) override;

    virtual qint32 getUser(
        User & user, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    virtual qint32 getAccountLimits(
        const qevercloud::ServiceLevel serviceLevel,
        qevercloud::AccountLimits & limits, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

private:
    qint32 processEdamUserException(
        const qevercloud::EDAMUserException & userException,
        ErrorString & errorDescription) const;

    qint32 processEdamSystemException(
        const qevercloud::EDAMSystemException & systemException,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) const;

private:
    Q_DISABLE_COPY(UserStore)

private:
    qevercloud::IUserStorePtr m_pUserStore;
    QString m_authenticationToken;
    QList<QNetworkCookie> m_cookies;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_USER_STORE_H
