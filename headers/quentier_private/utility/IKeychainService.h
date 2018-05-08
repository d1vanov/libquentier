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

#ifndef LIB_QUENTIER_PRIVATE_UTILITY_I_KEYCHAIN_SERVICE_H
#define LIB_QUENTIER_PRIVATE_UTILITY_I_KEYCHAIN_SERVICE_H

#include <quentier/utility/Linkage.h>
#include <quentier/utility/Macros.h>
#include <quentier/types/ErrorString.h>
#include <QObject>
#include <QUuid>

namespace quentier {

class QUENTIER_EXPORT IKeychainService: public QObject
{
    Q_OBJECT
protected:
    explicit IKeychainService(QObject * parent = Q_NULLPTR) : QObject(parent) {}

public:
    virtual ~IKeychainService() {}

public:
    virtual QUuid startWritePasswordJob(const QString & service, const QString & key, const QString & password) = 0;
    virtual QUuid startReadPasswordJob(const QString & service, const QString & key) = 0;
    virtual QUuid startDeletePasswordJob(const QString & service, const QString & key) = 0;

Q_SIGNALS:
    void writePasswordJobFinished(QUuid requestId, bool status, ErrorString errorDescription);
    void readPasswordJobFinished(QUuid requestId, bool status, ErrorString errorDescription, QString password);
    void deletePasswordJobFinished(QUuid requestId, bool status, ErrorString errorDescription);

private:
    Q_DISABLE_COPY(IKeychainService);
};

} // namespace quentier

#endif // LIB_QUENTIER_PRIVATE_UTILITY_I_KEYCHAIN_SERVICE_H
