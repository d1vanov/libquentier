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

#ifndef LIB_QUENTIER_TYPES_I_FAVORITABLE_DATA_ELEMENT_H
#define LIB_QUENTIER_TYPES_I_FAVORITABLE_DATA_ELEMENT_H

#include "INoteStoreDataElement.h"

namespace quentier {

/**
 * Class adding one more attribute to each note store data element subclassing
 * it: "favorited" flag. Favorited data elements are expected to be somehow
 * arranged for quick access in the client application's UI.
 */
class QUENTIER_EXPORT IFavoritableDataElement :
    public virtual INoteStoreDataElement
{
public:
    virtual bool isFavorited() const = 0;
    virtual void setFavorited(const bool favorited) = 0;

    virtual ~IFavoritableDataElement() {}
};

#define DECLARE_IS_FAVORITED                                                   \
    virtual bool isFavorited() const override;                                 \
    // DECLARE_IS_FAVORITED

#define DECLARE_SET_FAVORITED                                                  \
    virtual void setFavorited(const bool favorited) override;                  \
    // DECLARE_SET_FAVORITED

#define QN_DECLARE_FAVORITED                                                   \
    DECLARE_IS_FAVORITED                                                       \
    DECLARE_SET_FAVORITED                                                      \
    // QN_DECLARE_FAVORITED

#define DEFINE_IS_FAVORITED(type)                                              \
    bool type::isFavorited() const                                             \
    {                                                                          \
        return d->m_isFavorited;                                               \
    }                                                                          \
    // DEFINE_IS_FAVORITED

#define DEFINE_SET_FAVORITED(type)                                             \
    void type::setFavorited(const bool favorited)                              \
    {                                                                          \
        d->m_isFavorited = favorited;                                          \
    }                                                                          \
    // DEFINE_SET_FAVORITED

#define QN_DEFINE_FAVORITED(type)                                              \
    DEFINE_IS_FAVORITED(type)                                                  \
    DEFINE_SET_FAVORITED(type)                                                 \
    // QN_DEFINE_FAVORITED

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_I_FAVORITABLE_DATA_ELEMENT_H
