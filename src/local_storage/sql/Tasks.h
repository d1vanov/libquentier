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

#include "ConnectionPool.h"
#include "Fwd.h"

#include <quentier/exception/DatabaseRequestException.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/types/ErrorString.h>

#include <utility/Threading.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <utility/Qt5Promise.h>
#endif

#include <QFuture>
#include <QThreadPool>

#include <memory>
#include <optional>

namespace quentier::local_storage::sql {

struct TaskContext
{
    QThreadPool *       m_threadPool = nullptr;
    QThreadPtr          m_writerThread;
    ConnectionPoolPtr   m_connectionPool;
    ErrorString         m_holderIsDeadErrorMessage;
    ErrorString         m_requestCanceledErrorMessage;
};

template <class ResultType, class FunctionType, class HolderType>
QFuture<ResultType> makeReadTask(
    TaskContext taskContext, std::weak_ptr<HolderType> holder_weak,
    FunctionType f)
{
    Q_ASSERT(taskContext.m_threadPool);
    Q_ASSERT(taskContext.m_connectionPool);

    auto promise = std::make_shared<QPromise<ResultType>>();
    auto future = promise->future();

    promise->start();

    auto * runnable = utility::createFunctionRunnable(
        [promise = std::move(promise), holder_weak = std::weak_ptr(holder_weak),
         taskContext = std::move(taskContext), f = std::move(f)] () mutable
        {
            const auto holder = holder_weak.lock();
            if (!holder) {
                promise->setException(RuntimeError{
                    std::move(taskContext.m_holderIsDeadErrorMessage)});
                promise->finish();
                return;
            }

            if (promise->isCanceled()) {
                promise->setException(RuntimeError{
                    std::move(taskContext.m_requestCanceledErrorMessage)});
                promise->finish();
                return;
            }

            auto databaseConnection = taskContext.m_connectionPool->database();

            ErrorString errorDescription;
            std::optional<ResultType> result = f(
                *holder, databaseConnection, errorDescription);

            if (result) {
                promise->addResult(std::move(*result));
            }
            else if (!errorDescription.isEmpty()) {
                promise->setException(
                    DatabaseRequestException{errorDescription});
            }

            promise->finish();
        });

    taskContext.m_threadPool->start(runnable);
    return future;
}

template <class ResultType, class FunctionType, class HolderType>
QFuture<ResultType> makeWriteTask(
    TaskContext taskContext, std::weak_ptr<HolderType> holder_weak,
    FunctionType f)
{
    Q_ASSERT(taskContext.m_writerThread);
    Q_ASSERT(taskContext.m_connectionPool);

    auto promise = std::make_shared<QPromise<ResultType>>();
    auto future = promise->future();

    promise->start();

    auto * writerThread = taskContext.m_writerThread.get();
    utility::postToThread(
        writerThread,
        [promise = std::move(promise), holder_weak = std::weak_ptr(holder_weak),
         taskContext = std::move(taskContext), f = std::move(f)] () mutable
         {
             const auto holder = holder_weak.lock();
             if (!holder) {
                 promise->setException(RuntimeError{
                     std::move(taskContext.m_holderIsDeadErrorMessage)});
                 promise->finish();
                 return;
             }

             auto databaseConnection = taskContext.m_connectionPool->database();

             ErrorString errorDescription;
             const bool res = f(
                 *holder, databaseConnection, errorDescription);

             if (!res) {
                 promise->setException(
                     DatabaseRequestException{errorDescription});
             }

             promise->finish();
         });

    return future;
}

} // namespace quentier::local_storage::sql
