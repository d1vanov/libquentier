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

#include <QFutureInterface>

#include <type_traits>

// Partial backport of QPromise from Qt6 to Qt5
template <typename T>
class QPromise
{
    static_assert(
        std::is_copy_constructible_v<T> || std::is_move_constructible_v<T> ||
            std::is_same_v<T, void>,
        "Type with copy or move constructors or type void is required");

public:
    QPromise() = default;

    Q_DISABLE_COPY(QPromise)

    QPromise(QPromise<T> && other) noexcept : d(other.d)
    {
        other.d = QFutureInterface<T>();
    }

    QPromise(QFutureInterface<T> & other) : d(other) {}

    QPromise & operator=(QPromise<T> && other) noexcept
    {
        QPromise<T> tmp(std::move(other));
        tmp.swap(*this);
        return *this;
    }

    ~QPromise()
    {
        // If QFutureInterface has no state, there is nothing to be done
        if (d.queryState(QFutureInterfaceBase::State::NoState)) {
            return;
        }

        // Otherwise, if computation is not finished at this point, cancel
        // potential waits
        if (!d.queryState(QFutureInterfaceBase::State::Finished)) {
            d.cancel();
            finish(); // required to finalize the state
        }
    }

    // Core QPromise APIs
    QFuture<T> future() const
    {
        return d.future();
    }
    template <
        typename U,
        typename = std::enable_if_t<
            std::is_same_v<U, T> || std::is_convertible_v<U, T>>>
    void addResult(U && result, int index = -1)
    {
        d.reportResult(std::forward<U>(result), index);
    }

    void setException(const QException & e)
    {
        d.reportException(e);
    }

    void start()
    {
        d.reportStarted();
    }
    void finish()
    {
        d.reportFinished();
    }

    void suspendIfRequested()
    {
        d.suspendIfRequested();
    }

    bool isCanceled() const
    {
        return d.isCanceled();
    }

    // Progress methods
    void setProgressRange(int minimum, int maximum)
    {
        d.setProgressRange(minimum, maximum);
    }
    void setProgressValue(int progressValue)
    {
        d.setProgressValue(progressValue);
    }
    void setProgressValueAndText(
        int progressValue, const QString & progressText)
    {
        d.setProgressValueAndText(progressValue, progressText);
    }

    void swap(QPromise<T> & other) noexcept
    {
        qSwap(this->d, other.d);
    }

private:
    mutable QFutureInterface<T> d = QFutureInterface<T>();
};

template <typename T>
inline void swap(QPromise<T> & a, QPromise<T> & b) noexcept
{
    a.swap(b);
}
