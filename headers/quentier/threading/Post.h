/*
 * Copyright 2021-2024 Dmitry Ivanov
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

#include <QAbstractEventDispatcher>
#include <QMetaObject>
#include <QObject>

#include <QThread>

#include <memory>
#include <utility>

namespace quentier::threading {

template <typename Function>
void postToObject(QObject * object, Function && function)
{
    Q_ASSERT(object);

    QMetaObject::invokeMethod(
        object, std::forward<Function>(function), Qt::QueuedConnection);
}

template <typename Function>
void postToThread(QThread * pThread, Function && function)
{
    Q_ASSERT(pThread);
    Q_ASSERT(!pThread->isFinished());

    QObject * pObject = QAbstractEventDispatcher::instance(pThread);
    if (!pObject) {
        // Thread's event loop has not been started yet. Create a dummy QObject,
        // move it to the target thread, set things up so that it would be
        // destroyed after the job is done and use postToObject.
        auto pDummyObj = std::make_unique<QObject>();
        pDummyObj->moveToThread(pThread);
        postToObject(
            pDummyObj.get(),
            [pObj = pDummyObj.get(),
             function = std::forward<Function>(function)]() mutable {
                pObj->deleteLater();
                function();
            });
        Q_UNUSED(pDummyObj.release()) // NOLINT
        return;
    }

    if (pThread == QThread::currentThread()) {
        // Already on the target thread, executing the function right away
        function();
        return;
    }

    QMetaObject::invokeMethod(pObject, std::forward<Function>(function));
}

} // namespace quentier::threading
