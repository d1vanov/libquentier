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

#ifndef LIB_QUENTIER_UTILITY_PRINTABLE_H
#define LIB_QUENTIER_UTILITY_PRINTABLE_H

#include <quentier/utility/Linkage.h>

#include <QDebug>
#include <QHash>
#include <QSet>
#include <QString>
#include <QTextStream>

namespace quentier {

/**
 * @brief The Printable class is the interface for Quentier's internal classes
 * which should be able to write themselves into QTextStream and/or convert
 * to QString
 */
class QUENTIER_EXPORT Printable
{
public:
    virtual QTextStream & print(QTextStream & strm) const = 0;

    virtual const QString toString() const;

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const Printable & printable);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & debug, const Printable & printable);

protected:
    Printable();
    Printable(const Printable & other);
    Printable & operator=(const Printable & other);
    virtual ~Printable();
};

} // namespace quentier

// printing operators for existing classes not inheriting from Printable

template <class T>
const QString ToString(const T & object)
{
    QString str;
    QTextStream strm(&str, QIODevice::WriteOnly);
    strm << object;
    return str;
}

template <class TKey, class TValue>
const QString ToString(const QHash<TKey, TValue> & object)
{
    QString str;
    QTextStream strm(&str, QIODevice::WriteOnly);
    strm << QStringLiteral("QHash: \n");

    typedef typename QHash<TKey, TValue>::const_iterator CIter;
    CIter hashEnd = object.end();
    for (CIter it = object.begin(); it != hashEnd; ++it) {
        strm << QStringLiteral("[") << it.key() << QStringLiteral("] = ")
             << it.value() << QStringLiteral(";\n");
    }
    return str;
}

template <class T>
const QString ToString(const QSet<T> & object)
{
    QString str;
    QTextStream strm(&str, QIODevice::WriteOnly);
    strm << QStringLiteral("QSet: \n");

    typedef typename QSet<T>::const_iterator CIter;
    CIter setEnd = object.end();
    for (CIter it = object.begin(); it != setEnd; ++it) {
        strm << QStringLiteral("[") << *it << QStringLiteral("];\n");
    }
    return str;
}

#define QUENTIER_DECLARE_PRINTABLE(type, ...)                                  \
    QUENTIER_EXPORT QTextStream & operator<<(                                  \
        QTextStream & strm, const type & obj);                                 \
    inline QDebug & operator<<(QDebug & debug, const type & obj)               \
    {                                                                          \
        debug << ToString<type, ##__VA_ARGS__>(obj);                           \
        return debug;                                                          \
    }                                                                          \
    // QUENTIER_DECLARE_PRINTABLE

#endif // LIB_QUENTIER_UTILITY_PRINTABLE_H
