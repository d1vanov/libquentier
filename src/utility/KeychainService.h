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

#ifndef LIB_QUENTIER_UTILITY_KEYCHAIN_SERVICE_H
#define LIB_QUENTIER_UTILITY_KEYCHAIN_SERVICE_H

#include <quentier_private/utility/IKeychainService.h>
#include <QHash>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5keychain/keychain.h>
#else
#include <qtkeychain/keychain.h>
#endif

namespace quentier {

class KeychainService: public IKeychainService
{
    Q_OBJECT
public:
    explicit KeychainService(QObject * parent = Q_NULLPTR);
    virtual ~KeychainService();

    virtual QUuid startWritePasswordJob(const QString & service, const QString & key, const QString & password) Q_DECL_OVERRIDE;
    virtual QUuid startReadPasswordJob(const QString & service, const QString & key) Q_DECL_OVERRIDE;
    virtual QUuid startDeletePasswordJob(const QString & service, const QString & key) Q_DECL_OVERRIDE;

private Q_SLOTS:
    void onWritePasswordJobFinished(QKeychain::Job * pJob);
    void onReadPasswordJobFinished(QKeychain::Job * pJob);
    void onDeletePasswordJobFinished(QKeychain::Job * pJob);

private:
    ErrorCode::type translateErrorCode(const QKeychain::Error errorCode) const;

private:
    QHash<QKeychain::ReadPasswordJob*, QUuid>   m_readPasswordJobs;
    QHash<QKeychain::WritePasswordJob*, QUuid>  m_writePasswordJobs;
    QHash<QKeychain::DeletePasswordJob*, QUuid> m_deletePasswordJobs;
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_KEYCHAIN_SERVICE_H
