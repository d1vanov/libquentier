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

#ifndef LIB_QUENTIER_UTILITY_QUENTIER_CHECK_PTR_H
#define LIB_QUENTIER_UTILITY_QUENTIER_CHECK_PTR_H

#include <quentier/exception/NullPtrException.h>
#include <quentier/logging/QuentierLogger.h>

#ifndef QUENTIER_CHECK_PTR
#define QUENTIER_CHECK_PTR(component, pointer, ...)                            \
    {                                                                          \
        if (Q_UNLIKELY(!pointer)) {                                            \
            using quentier::NullPtrException;                                  \
            ErrorString quentier_null_ptr_error(                               \
                QT_TRANSLATE_NOOP("", "Detected unintended null pointer"));    \
            quentier_null_ptr_error.details() = QStringLiteral(__FILE__);      \
            quentier_null_ptr_error.details() += QStringLiteral(" (");         \
            quentier_null_ptr_error.details() += QString::number(__LINE__);    \
            quentier_null_ptr_error.details() += QStringLiteral(") ");         \
            quentier_null_ptr_error.details() +=                               \
                QString::fromUtf8("" #__VA_ARGS__ "");                         \
            QNERROR(component, quentier_null_ptr_error);                       \
            throw NullPtrException(quentier_null_ptr_error);                   \
        }                                                                      \
    }                                                                          \
// QUENTIER_CHECK_PTR
#endif

#endif // LIB_QUENTIER_UTILITY_QUENTIER_CHECK_PTR_H
