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

#ifndef LIB_QUENTIER_TYPES_I_LOCAL_STORAGE_DATA_ELEMENT_H
#define LIB_QUENTIER_TYPES_I_LOCAL_STORAGE_DATA_ELEMENT_H

#include <quentier/utility/Linkage.h>
#include <quentier/utility/UidGenerator.h>

#include <QString>
#include <QUuid>

namespace quentier {

class QUENTIER_EXPORT ILocalStorageDataElement
{
public:
    virtual const QString localUid() const = 0;
    virtual void setLocalUid(const QString & guid) = 0;
    virtual void unsetLocalUid() = 0;

    virtual ~ILocalStorageDataElement() {}
};

#define DEFINE_LOCAL_UID_GETTER(type)                                          \
    const QString type::localUid() const                                       \
    {                                                                          \
        return UidGenerator::UidToString(d->m_localUid);                       \
    }                                                                          \
    // DEFINE_LOCAL_UID_GETTER

#define DEFINE_LOCAL_UID_SETTER(type)                                          \
    void type::setLocalUid(const QString & uid)                                \
    {                                                                          \
        d->m_localUid = uid;                                                   \
    }                                                                          \
    // DEFINE_LOCAL_UID_SETTER

#define DEFINE_LOCAL_UID_UNSETTER(type)                                        \
    void type::unsetLocalUid()                                                 \
    {                                                                          \
        d->m_localUid = QUuid();                                               \
    }                                                                          \
    // DEFINE_LOCAL_UID_UNSETTER

#define QN_DECLARE_LOCAL_UID                                                   \
    virtual const QString localUid() const override;                           \
    virtual void setLocalUid(const QString & guid) override;                   \
    virtual void unsetLocalUid() override;                                     \
    // QN_DECLARE_LOCAL_UID

#define QN_DEFINE_LOCAL_UID(type)                                              \
    DEFINE_LOCAL_UID_GETTER(type)                                              \
    DEFINE_LOCAL_UID_SETTER(type)                                              \
    DEFINE_LOCAL_UID_UNSETTER(type)                                            \
    // QN_DEFINE_LOCAL_UID

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_I_LOCAL_STORAGE_DATA_ELEMENT_H
