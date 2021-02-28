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

#include <quentier/types/ErrorString.h>
#include <quentier/utility/Compat.h>

#include "data/ErrorStringData.h"

#include <QApplication>

namespace quentier {

ErrorString::ErrorString(const char * error) :
    Printable(), d(new ErrorStringData)
{
    d->m_base = QString::fromUtf8(error);
}

ErrorString::ErrorString(const QString & error) :
    Printable(), d(new ErrorStringData)
{
    d->m_base = error;
}

ErrorString::ErrorString(const ErrorString & other) : Printable(), d(other.d) {}

ErrorString & ErrorString::operator=(const ErrorString & other)
{
    if (this != &other) {
        d = other.d;
    }

    return *this;
}

ErrorString::~ErrorString() {}

const QString & ErrorString::base() const
{
    return d->m_base;
}

QString & ErrorString::base()
{
    return d->m_base;
}

const QStringList & ErrorString::additionalBases() const
{
    return d->m_additionalBases;
}

QStringList & ErrorString::additionalBases()
{
    return d->m_additionalBases;
}

const QString & ErrorString::details() const
{
    return d->m_details;
}

QString & ErrorString::details()
{
    return d->m_details;
}

void ErrorString::setBase(const QString & error)
{
    d->m_base = error;
}

void ErrorString::setBase(const char * error)
{
    d->m_base = QString::fromUtf8(error);
}

void ErrorString::appendBase(const QString & error)
{
    d->m_additionalBases.append(error);
}

void ErrorString::appendBase(const QStringList & errors)
{
    d->m_additionalBases.append(errors);
}

void ErrorString::appendBase(const char * error)
{
    appendBase(QString::fromUtf8(error));
}

void ErrorString::setDetails(const QString & error)
{
    d->m_details = error;
}

void ErrorString::setDetails(const char * error)
{
    d->m_details = QString::fromUtf8(error);
}

bool ErrorString::isEmpty() const
{
    return d->m_base.isEmpty() && d->m_details.isEmpty() &&
        d->m_additionalBases.isEmpty();
}

void ErrorString::clear()
{
    d->m_base.clear();
    d->m_details.clear();
    d->m_additionalBases.clear();
}

QString ErrorString::localizedString() const
{
    if (isEmpty()) {
        return QString();
    }

    QString baseStr;
    if (!d->m_base.isEmpty()) {
        baseStr = qApp->translate("", d->m_base.toLocal8Bit().constData());
    }

    QString additionalBasesStr;
    for (const auto & additionalBase: qAsConst(d->m_additionalBases)) {
        if (additionalBase.isEmpty()) {
            continue;
        }

        QString translatedStr =
            qApp->translate("", additionalBase.toLocal8Bit().constData());

        if (additionalBasesStr.isEmpty()) {
            additionalBasesStr = translatedStr;
        }
        else {
            additionalBasesStr += QStringLiteral(", ");
            additionalBasesStr += translatedStr;
        }
    }

    QString result = baseStr;
    if (!result.isEmpty()) {
        // Capitalize the first letter
        result = result.at(0).toUpper() + result.mid(1);
    }

    if (!result.isEmpty() && !additionalBasesStr.isEmpty()) {
        result += QStringLiteral(", ");
    }

    if (result.isEmpty()) {
        result += additionalBasesStr;

        if (!result.isEmpty()) {
            // Capitalize the first letter
            result = result.at(0).toUpper() + result.mid(1);
        }
    }
    else {
        result += additionalBasesStr.toLower();
    }

    if (d->m_details.isEmpty()) {
        return result;
    }

    if (!result.isEmpty()) {
        result += QStringLiteral(": ");
        result += d->m_details.toLower();
        return result;
    }

    result += d->m_details;
    if (!result.isEmpty()) {
        // Capitalize the first letter
        result = result.at(0).toUpper() + result.mid(1);
    }

    return result;
}

QString ErrorString::nonLocalizedString() const
{
    QString result = d->m_base;

    for (const auto & additionalBase: qAsConst(d->m_additionalBases)) {
        if (additionalBase.isEmpty()) {
            continue;
        }

        if (Q_LIKELY(!result.isEmpty())) {
            result += QStringLiteral(", ");
        }

        result += additionalBase;
    }

    if (d->m_details.isEmpty()) {
        return result;
    }

    if (!result.isEmpty()) {
        result += QStringLiteral(": ");
    }

    result += d->m_details;
    return result;
}

QTextStream & ErrorString::print(QTextStream & strm) const
{
    strm << d->m_base;

    for (auto it = d->m_additionalBases.constBegin(),
              end = d->m_additionalBases.constEnd();
         it != end; ++it)
    {
        QString previousStr = d->m_base;
        if (Q_LIKELY(it != d->m_additionalBases.constBegin())) {
            auto prevIt = it;
            --prevIt;
            previousStr = *prevIt;
        }

        if (Q_UNLIKELY(previousStr.isEmpty())) {
            strm << *it;
        }
        else {
            strm << ", " << *it;
        }
    }

    if (!d->m_details.isEmpty()) {
        strm << ": " << d->m_details;
    }

    return strm;
}

} // namespace quentier
