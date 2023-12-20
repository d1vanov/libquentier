/*
 * Copyright 2023 Dmitry Ivanov
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

#include <quentier/exception/RuntimeError.h>
#include <quentier/types/ErrorString.h>

#include <type_traits>
#include <variant>

namespace quentier {

/**
 * @brief The Result template class represents the bare bones result monad
 * implementation which either contains some valid value or an error.
 */
template <
    class T, class Error,
    typename = typename std::enable_if_t<
        !std::is_same_v<std::decay_t<T>, std::decay_t<Error>>>>
class Result
{
    using ValueType = std::conditional_t<
        std::is_same_v<std::decay_t<T>, void>, std::monostate, T>;

public:
    template <
        typename T1 = T,
        typename std::enable_if_t<!std::is_void_v<std::decay_t<T1>>> * = nullptr>
    explicit Result(T1 t) : m_valueOrError{std::move(t)}
    {}

    template <
        typename T1 = T,
        typename std::enable_if_t<std::is_void_v<std::decay_t<T1>>> * = nullptr>
    explicit Result() : m_valueOrError{std::monostate{}}
    {}

    explicit Result(Error error) : m_valueOrError{std::move(error)} {}

    Result(const Result<T, Error> & other) = default;
    Result(Result<T, Error> && other) = default;

    Result & operator=(const Result<T, Error> & other) = default;
    Result & operator=(Result<T, Error> && other) = default;

    /**
     * @return boolean value indicating whether the result contains a value
     */
    [[nodiscard]] bool isValid() const noexcept
    {
        return std::holds_alternative<ValueType>(m_valueOrError);
    }

    operator bool() const noexcept
    {
        return isValid();
    }

    template <
        typename T1 = T,
        typename std::enable_if_t<!std::is_void_v<std::decay_t<T1>>> * = nullptr>
    [[nodiscard]] T1 & get()
    {
        // NOTE: std::get also performs the check of what is stored inside the
        // variant but it throws std::bad_variant_access which doesn't implement
        // QException so this exception is not representable inside QFuture
        // in Qt5. Due to this for Qt5 also performing another check and using
        // another exception type
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        if (Q_UNLIKELY(!isValid())) {
            throw RuntimeError{
                ErrorString{"Detected attempt to get value from empty Result"}};
        }
#endif

        return std::get<T>(m_valueOrError);
    }

    template <
        typename T1 = T,
        typename std::enable_if_t<!std::is_void_v<std::decay_t<T1>>> * = nullptr>
    [[nodiscard]] const T1 & get() const
    {
        // NOTE: std::get also performs the check of what is stored inside the
        // variant but it throws std::bad_variant_access which doesn't implement
        // QException so this exception is not representable inside QFuture
        // in Qt5. Due to this for Qt5 also performing another check and using
        // another exception type
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        if (Q_UNLIKELY(!isValid())) {
            throw RuntimeError{
                ErrorString{"Detected attempt to get value from empty Result"}};
        }
#endif

        return std::get<T>(m_valueOrError);
    }

    template <
        typename T1 = T,
        typename std::enable_if_t<!std::is_void_v<std::decay_t<T1>>> * = nullptr>
    [[nodiscard]] T1 & operator*()
    {
        return get();
    }

    template <
        typename T1 = T,
        typename std::enable_if_t<!std::is_void_v<std::decay_t<T1>>> * = nullptr>
    [[nodiscard]] const T1 & operator*() const
    {
        return get();
    }

    [[nodiscard]] const Error & error() const
    {
        // NOTE: std::get also performs the check of what is stored inside the
        // variant but it throws std::bad_variant_access which doesn't implement
        // QException so this exception is not representable inside QFuture
        // in Qt5. Due to this for Qt5 also performing another check and using
        // another exception type
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        if (Q_UNLIKELY(isValid())) {
            throw RuntimeError{ErrorString{
                "Detected attempt to get error from non-empty Result"}};
        }
#endif

        return std::get<Error>(m_valueOrError);
    }

    [[nodiscard]] Error & error()
    {
        // NOTE: std::get also performs the check of what is stored inside the
        // variant but it throws std::bad_variant_access which doesn't implement
        // QException so this exception is not representable inside QFuture
        // in Qt5. Due to this for Qt5 also performing another check and using
        // another exception type
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        if (Q_UNLIKELY(isValid())) {
            throw RuntimeError{ErrorString{
                "Detected attempt to get error from non-empty Result"}};
        }
#endif

        return std::get<Error>(m_valueOrError);
    }

private:
    std::variant<ValueType, Error> m_valueOrError;
};

} // namespace quentier
