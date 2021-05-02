/*
 * Copyright 2021 Dmitry Ivanov
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

#pragma once

#include <quentier/utility/Linkage.h>

#include <QAbstractEventDispatcher>
#include <QFuture>
#include <QObject>

#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
#include <QMetaObject>
#else
#include <QThread>
#endif

#include <functional>

class QRunnable;

namespace quentier::utility {

template <typename Function>
void postToObject(QObject * pObject, Function && function)
{
    Q_ASSERT(pObject);

#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    QMetaObject::invokeMethod(pObject, std::forward<Function>(function));
#else
    QObject src;
    QObject::connect(
        &src, &QObject::destroyed, pObject, std::forward<Function>(function),
        Qt::QueuedConnection);
#endif
}

template <typename Function>
void postToThread(QThread * pThread, Function && function)
{
    Q_ASSERT(pThread);

    QObject * pObject = QAbstractEventDispatcher::instance(pThread);
    Q_ASSERT(pObject);

#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    QMetaObject::invokeMethod(pObject, std::forward<Function>(function));
#else
    QObject src;
    QObject::connect(
        &src, &QObject::destroyed, pObject, std::forward<Function>(function),
        Qt::QueuedConnection);
#endif
}

/**
 * Create QRunnable from a function - sort of a workaround for Qt < 5.15
 * where QRunnable::create does the same job
 */
[[nodiscard]] QUENTIER_EXPORT QRunnable * createFunctionRunnable(
    std::function<void()> function);

} // namespace quentier::utility
