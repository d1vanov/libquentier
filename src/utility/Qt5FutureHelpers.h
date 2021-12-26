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

#include <QFuture>

#include <type_traits>

// Backports of some helpers for QFuture continuations from Qt6 to Qt5
namespace QtFuture {

// Inherit option from Qt6 is not supported in Qt5
enum class Launch
{
    Sync,
    Async,
};

} // namespace QtFuture

namespace quentier::utility {

template <typename F, typename Arg, typename Enable = void>
struct ResultTypeHelper
{};

// The callable takes an argument of type Arg
template <typename F, typename Arg>
struct ResultTypeHelper<
    F, Arg,
    typename std::enable_if_t<
        !std::is_invocable_v<std::decay_t<F>, QFuture<Arg>>>>
{
    using ResultType = std::invoke_result_t<std::decay_t<F>, std::decay_t<Arg>>;
};

// The callable takes an argument of type QFuture<Arg>
template <class F, class Arg>
struct ResultTypeHelper<
    F, Arg,
    typename std::enable_if_t<
        std::is_invocable_v<std::decay_t<F>, QFuture<Arg>>>>
{
    using ResultType = std::invoke_result_t<std::decay_t<F>, QFuture<Arg>>;
};

// The callable takes an argument of type QFuture<void>
template <class F>
struct ResultTypeHelper<
    F, void,
    typename std::enable_if_t<
        std::is_invocable_v<std::decay_t<F>, QFuture<void>>>>
{
    using ResultType = std::invoke_result_t<std::decay_t<F>, QFuture<void>>;
};

// The callable doesn't take argument
template <class F>
struct ResultTypeHelper<
    F, void,
    typename std::enable_if_t<
        !std::is_invocable_v<std::decay_t<F>, QFuture<void>>>>
{
    using ResultType = std::invoke_result_t<std::decay_t<F>>;
};

} // namespace quentier::utility
