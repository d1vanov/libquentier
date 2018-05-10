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

#ifndef LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_KEYCHAIN_SERVICE_H
#define LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_KEYCHAIN_SERVICE_H

#include <quentier_private/utility/IKeychainService.h>
#include <QHash>
#include <utility>

namespace quentier {

class FakeKeychainService: public IKeychainService
{
    Q_OBJECT
public:
    explicit FakeKeychainService(QObject * parent = Q_NULLPTR);
    virtual ~FakeKeychainService();

    virtual QUuid startWritePasswordJob(const QString & service, const QString & key, const QString & password) Q_DECL_OVERRIDE;
    virtual QUuid startReadPasswordJob(const QString & service, const QString & key) Q_DECL_OVERRIDE;
    virtual QUuid startDeletePasswordJob(const QString & service, const QString & key) Q_DECL_OVERRIDE;

private:
    virtual void timerEvent(QTimerEvent * pEvent) Q_DECL_OVERRIDE;

private:
    QHash<int, QUuid>                       m_writePasswordRequestIdByTimerId;
    QHash<int, std::pair<QUuid, QString> >  m_readPasswordRequestIdWithPasswordByTimerId;
    QHash<int, std::pair<QUuid, bool> >     m_deletePasswordRequestIdByTimerId;
};

} // namespace quentier

#endif // LIB_QUENTIER_TESTS_SYNCHRONIZATION_FAKE_KEYCHAIN_SERVICE_H
