/*
 * Copyright 2023-2024 Dmitry Ivanov
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

template <
    class ValueType, class ErrorType,
    typename =
        typename std::enable_if_t<!std::is_void_v<std::decay_t<ErrorType>>>,
    typename = typename std::enable_if_t<
        !std::is_same_v<std::decay_t<ValueType>, std::decay_t<ErrorType>>>>
class Result
{
private:
    template <class T>
    struct ValueWrapper
    {
        T value;
    };

    template <>
    struct ValueWrapper<void>
    {
    };

public:
    template <
        typename T1 = ValueType,
        typename std::enable_if_t<!std::is_void_v<std::decay_t<T1>>> * =
            nullptr>
    explicit Result(T1 t) :
        m_valueOrError{ValueWrapper<std::decay_t<ValueType>>{std::move(t)}}
    {}

    template <
        typename T1 = ValueType,
        typename std::enable_if_t<std::is_void_v<std::decay_t<T1>>> * = nullptr>
    explicit Result() : m_valueOrError{ValueWrapper<void>{}}
    {}

    explicit Result(ErrorType error) : m_valueOrError{std::move(error)} {}

    Result(const Result<ValueType, ErrorType> & other) :
        m_valueOrError{other.m_valueOrError}
    {}

    Result(Result<ValueType, ErrorType> && other) :
        m_valueOrError{std::move(other.m_valueOrError)}
    {}

    Result & operator=(const Result<ValueType, ErrorType> & other)
    {
        if (this != &other) {
            m_valueOrError = other.m_valueOrError;
        }

        return *this;
    }

    Result & operator=(Result<ValueType, ErrorType> && other)
    {
        if (this != &other) {
            m_valueOrError = std::move(other.m_valueOrError);
        }

        return *this;
    }

    /**
     * @return boolean value indicating whether the result contains a value
     */
    [[nodiscard]] bool isValid() const noexcept
    {
        return std::holds_alternative<ValueWrapper<std::decay_t<ValueType>>>(
            m_valueOrError);
    }

    operator bool() const noexcept
    {
        return isValid();
    }

    template <
        typename T1 = ValueType,
        typename std::enable_if_t<!std::is_void_v<std::decay_t<T1>>> * =
            nullptr>
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

        return std::get<ValueWrapper<std::decay_t<ValueType>>>(m_valueOrError)
            .value;
    }

    template <
        typename T1 = ValueType,
        typename std::enable_if_t<!std::is_void_v<std::decay_t<T1>>> * =
            nullptr>
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

        return std::get<ValueWrapper<std::decay_t<ValueType>>>(m_valueOrError)
            .value;
    }

    template <
        typename T1 = ValueType,
        typename std::enable_if_t<!std::is_void_v<std::decay_t<T1>>> * =
            nullptr>
    [[nodiscard]] T1 & operator*()
    {
        return get();
    }

    template <
        typename T1 = ValueType,
        typename std::enable_if_t<!std::is_void_v<std::decay_t<T1>>> * =
            nullptr>
    [[nodiscard]] const T1 & operator*() const
    {
        return get();
    }

    [[nodiscard]] const ErrorType & error() const
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

        return std::get<ErrorType>(m_valueOrError);
    }

    [[nodiscard]] ErrorType & error()
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

        return std::get<ErrorType>(m_valueOrError);
    }

private:
    std::variant<ValueWrapper<std::decay_t<ValueType>>, ErrorType>
        m_valueOrError;
};

} // namespace quentier
