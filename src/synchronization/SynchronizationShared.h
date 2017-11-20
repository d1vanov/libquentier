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

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5qevercloud/QEverCloud.h>
#else
#include <qt4qevercloud/QEverCloud.h>
#endif

#include <QVector>
#include <QString>

#define SYNCHRONIZATION_PERSISTENCE_NAME QStringLiteral("SynchronizationPersistence")

#define HALF_AN_HOUR_IN_MSEC (1800000)

// NOTE: Workaround a bug in Qt4 which may prevent building with some boost versions
#ifndef Q_MOC_RUN
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#endif

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

template <typename T>
class OptionalComparator
{
public:
    bool operator()(const qevercloud::Optional<T> & lhs, const qevercloud::Optional<T> & rhs) const
    {
        if (!lhs.isSet() && !rhs.isSet()) {
            return false;
        }
        else if (!lhs.isSet() && rhs.isSet()) {
            return true;
        }
        else if (lhs.isSet() && !rhs.isSet()) {
            return false;
        }
        else {
            return lhs.ref() < rhs.ref();
        }
    }
};

class OptionalStringCaseInsensitiveComparator
{
public:
    bool operator()(const qevercloud::Optional<QString> & lhs, const qevercloud::Optional<QString> & rhs) const
    {
        if (!lhs.isSet() && !rhs.isSet()) {
            return false;
        }
        else if (!lhs.isSet() && rhs.isSet()) {
            return true;
        }
        else if (lhs.isSet() && !rhs.isSet()) {
            return false;
        }
        else {
            return lhs.ref().toUpper() < rhs.ref().toUpper();
        }
    }
};

struct ByGuid{};
struct ByName{};
struct ByParentTagGuid{};

typedef boost::multi_index_container<
    qevercloud::Tag,
    boost::multi_index::indexed_by<
        boost::multi_index::ordered_unique<
            boost::multi_index::tag<ByGuid>,
            boost::multi_index::member<qevercloud::Tag,qevercloud::Optional<QString>,&qevercloud::Tag::guid>,
            OptionalComparator<QString>
        >,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<ByName>,
            boost::multi_index::member<qevercloud::Tag,qevercloud::Optional<QString>,&qevercloud::Tag::name>,
            OptionalStringCaseInsensitiveComparator
        >,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<ByParentTagGuid>,
            boost::multi_index::member<qevercloud::Tag,qevercloud::Optional<QString>,&qevercloud::Tag::parentGuid>,
            OptionalComparator<QString>
        >
    >
> TagsContainer;

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_SHARED_H
