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

#ifndef LIB_QUENTIER_UTILITY_TAG_SORT_BY_PARENT_CHILD_RELATIONS_HELPERS_HPP
#define LIB_QUENTIER_UTILITY_TAG_SORT_BY_PARENT_CHILD_RELATIONS_HELPERS_HPP

#include <quentier/utility/TagSortByParentChildRelations.h>

namespace quentier {

template <class T>
bool Q_DECL_HIDDEN tagHasGuid(const T & tag);

template <>
bool Q_DECL_HIDDEN tagHasGuid(const qevercloud::Tag & tag)
{
    return tag.guid.isSet();
}

template <>
bool Q_DECL_HIDDEN tagHasGuid(const Tag & tag)
{
    return tag.hasGuid();
}

template <class T>
QString Q_DECL_HIDDEN tagGuid(const T & tag);

template <>
QString Q_DECL_HIDDEN tagGuid(const qevercloud::Tag & tag)
{
    return tag.guid.ref();
}

template <>
QString Q_DECL_HIDDEN tagGuid(const Tag & tag)
{
    return tag.guid();
}

template <class T>
QString Q_DECL_HIDDEN tagParentGuid(const T & tag);

template <>
QString Q_DECL_HIDDEN tagParentGuid(const qevercloud::Tag & tag)
{
    if (tag.parentGuid.isSet()) {
        return tag.parentGuid.ref();
    }

    return QString();
}

template <>
QString Q_DECL_HIDDEN tagParentGuid(const Tag & tag)
{
    if (tag.hasParentGuid()) {
        return tag.parentGuid();
    }

    return QString();
}

template <class T>
class Q_DECL_HIDDEN CompareItemByGuid
{
public:
    CompareItemByGuid(const QString & guid) :
        m_guid(guid)
    {}

    bool operator()(const T & tag) const
    {
        if (!tagHasGuid(tag)) {
            return false;
        }

        return (m_guid == tagGuid(tag));
    }

private:
    QString     m_guid;
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_TAG_SORT_BY_PARENT_CHILD_RELATIONS_HELPERS_HPP
