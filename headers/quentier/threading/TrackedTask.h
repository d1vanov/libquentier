/*
 * Copyright 2022-2024 Dmitry Ivanov
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

#include <functional>
#include <type_traits>
#include <utility>

namespace quentier::threading {

namespace detail {

template <typename LockableObject, typename Function, typename... Arguments>
constexpr std::enable_if_t<std::is_invocable_v<Function, Arguments...>> invoke(
    LockableObject & lockableObject, Function & function,
    Arguments &&... arguments)
{
    const auto lockedObject = lockableObject.lock();
    if (lockedObject) {
        std::invoke(function, std::forward<Arguments>(arguments)...);
    }
}

template <typename LockableObject, typename Function, typename... Arguments>
constexpr std::enable_if_t<
    !std::is_invocable_v<Function, Arguments...> &&
    std::is_member_function_pointer_v<Function>> invoke(
        LockableObject & lockableObject, Function & function,
        Arguments &&... arguments)
{
    const auto lockedObject = lockableObject.lock();
    if (lockedObject) {
        std::invoke(
            function, *lockedObject, std::forward<Arguments>(arguments)...);
    }
}

} // namespace detail

/**
 * Wrapper class which automates checking for the state of a lockable object.
 * With this class code like this
 *
 * auto task = [selfWeak = weak_from_this()] {
 *     auto self = selfWeak.lock();
 *     if (!self) {
 *         return;
 *     }
 *     // otherwise do something
 * };
 *
 * can be written like this:
 *
 * auto task = threading::TrackedTask{weak_from_this(), &MyClass::someMethod};
 */
template <typename LockableObject, typename Function>
class TrackedTask
{
public:
    template <typename SomeLockableObject, typename SomeFunction>
    constexpr TrackedTask(
        SomeLockableObject && someLockableObject,
        SomeFunction && function) :
        m_lockableObject{std::forward<SomeLockableObject>(someLockableObject)},
        m_function{std::forward<SomeFunction>(function)}
    {}

    template<
        typename... Arguments,
        typename = std::enable_if_t<
            std::is_invocable_v<Function, Arguments...> ||
            std::is_member_function_pointer_v<Function>>>
    constexpr void operator()(Arguments &&... arguments)
    {
        detail::invoke(
            m_lockableObject, m_function,
            std::forward<Arguments>(arguments)...);
    }

    template<
        typename... Arguments,
        typename = std::enable_if_t<
            std::is_invocable_v<Function, Arguments...> ||
            std::is_member_function_pointer_v<Function>>>
    constexpr void operator()(Arguments &&... arguments) const
    {
        detail::invoke(
            m_lockableObject, m_function,
            std::forward<Arguments>(arguments)...);
    }

private:
    LockableObject m_lockableObject;
    Function m_function;
};

template <typename LockableObject, typename Function>
TrackedTask(LockableObject, Function)
    -> TrackedTask<LockableObject, Function>;

} // namespace quentier::threading
