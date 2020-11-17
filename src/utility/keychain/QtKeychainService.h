/*
 * Copyright 2018-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_UTILITY_KEYCHAIN_QT_KEYCHAIN_SERVICE_H
#define LIB_QUENTIER_UTILITY_KEYCHAIN_QT_KEYCHAIN_SERVICE_H

#include <quentier/utility/IKeychainService.h>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(QtKeychainWrapper)

class Q_DECL_HIDDEN QtKeychainService final : public IKeychainService
{
    Q_OBJECT
public:
    explicit QtKeychainService(QObject * parent = nullptr);

    virtual ~QtKeychainService() override;

    virtual QUuid startWritePasswordJob(
        const QString & service, const QString & key,
        const QString & password) override;

    virtual QUuid startReadPasswordJob(
        const QString & service, const QString & key) override;

    virtual QUuid startDeletePasswordJob(
        const QString & service, const QString & key) override;

Q_SIGNALS:
    // private signals
    void notifyStartWritePasswordJob(
        QUuid jobId, QString service, QString key, QString password);

    void notifyStartReadPasswordJob(QUuid jobId, QString service, QString key);

    void notifyStartDeletePasswordJob(
        QUuid jobId, QString service, QString key);

private:
    QtKeychainWrapper * m_pQtKeychainWrapper;
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_KEYCHAIN_QT_KEYCHAIN_SERVICE_H
