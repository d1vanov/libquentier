/*
 * Copyright 2017-2020 Dmitry Ivanov
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
bool Q_DECL_HIDDEN tagHasLocalUid(const T & tag);

template <>
bool Q_DECL_HIDDEN tagHasLocalUid(const qevercloud::Tag & tag)
{
    Q_UNUSED(tag)
    return false;
}

template <>
bool Q_DECL_HIDDEN tagHasLocalUid(const Tag & tag)
{
    return !tag.localUid().isEmpty();
}

template <class T>
QString Q_DECL_HIDDEN tagLocalUid(const T & tag);

template <>
QString Q_DECL_HIDDEN tagLocalUid(const qevercloud::Tag & tag)
{
    Q_UNUSED(tag)
    return QString();
}

template <>
QString Q_DECL_HIDDEN tagLocalUid(const Tag & tag)
{
    return tag.localUid();
}

template <class T>
QString Q_DECL_HIDDEN tagParentLocalUid(const T & tag);

template <>
QString Q_DECL_HIDDEN tagParentLocalUid(const qevercloud::Tag & tag)
{
    Q_UNUSED(tag)
    return QString();
}

template <>
QString Q_DECL_HIDDEN tagParentLocalUid(const Tag & tag)
{
    if (tag.hasParentLocalUid()) {
        return tag.parentLocalUid();
    }

    return QString();
}

template <class T>
class Q_DECL_HIDDEN CompareItemByGuid
{
public:
    CompareItemByGuid(QString guid) : m_guid(std::move(guid)) {}

    bool operator()(const T & tag) const
    {
        if (tagHasGuid(tag)) {
            return (m_guid == tagGuid(tag));
        }

        return false;
    }

private:
    QString m_guid;
};

template <class T>
class Q_DECL_HIDDEN CompareItemByLocalUid
{
public:
    CompareItemByLocalUid(QString localUid) : m_localUid(std::move(localUid)) {}

    bool operator()(const T & tag) const
    {
        if (tagHasLocalUid(tag)) {
            return (m_localUid == tagLocalUid(tag));
        }

        return false;
    }

private:
    QString m_localUid;
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_TAG_SORT_BY_PARENT_CHILD_RELATIONS_HELPERS_HPP
