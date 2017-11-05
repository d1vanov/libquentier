/*
 * Copyright 2017 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_SHARED_H
#define LIB_QUENTIER_SYNCHRONIZATION_SHARED_H

#include <quentier/utility/Printable.h>
#include <QVector>
#include <QString>

#define SYNCHRONIZATION_PERSISTENCE_NAME QStringLiteral("SynchronizationPersistence")

#define HALF_AN_HOUR_IN_MSEC (1800000)

namespace quentier {

class Q_DECL_HIDDEN LinkedNotebookAuthData: public Printable
{
public:
    LinkedNotebookAuthData();
    LinkedNotebookAuthData(const QString & guid,
                           const QString & shardId,
                           const QString & sharedNotebookGlobalId,
                           const QString & uri,
                           const QString & noteStoreUrl);

    virtual QTextStream & print(QTextStream & strm) const Q_DECL_OVERRIDE;

    QString     m_guid;
    QString     m_shardId;
    QString     m_sharedNotebookGlobalId;
    QString     m_uri;
    QString     m_noteStoreUrl;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_SHARED_H
