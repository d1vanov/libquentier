/*
 * Copyright 2024 Dmitry Ivanov
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

#include <synchronization/types/ExceptionUtils.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/OperationCanceled.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/LocalStorageOpenException.h>
#include <quentier/local_storage/LocalStorageOperationException.h>

#include <gtest/gtest.h>

#include <array>
#include <type_traits>
#include <variant>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

class MyException : public QException
{
public:
    explicit MyException(QString message) : m_message{std::move(message)} {}

public: // QException
    [[nodiscard]] QException * clone() const override
    {
        return new MyException{m_message};
    }

    void raise() const override
    {
        throw *this;
    }

private:
    const QString m_message;
};

using ExceptionVariant = std::variant<
    InvalidArgument, OperationCanceled, RuntimeError,
    local_storage::LocalStorageOpenException,
    local_storage::LocalStorageOperationException, MyException>;

class ExceptionVariantVisitor
{
public:
    explicit ExceptionVariantVisitor(ExceptionVariant v) :
        m_variant{std::move(v)}
    {}

    void operator()(const InvalidArgument & e)
    {
        visit(e);
    }

    void operator()(const OperationCanceled & e)
    {
        visit(e);
    }

    void operator()(const RuntimeError & e)
    {
        visit(e);
    }

    void operator()(const local_storage::LocalStorageOpenException & e)
    {
        visit(e);
    }

    void operator()(const local_storage::LocalStorageOperationException & e)
    {
        visit(e);
    }

    void operator()(const MyException & e)
    {
        visit(e);
    }

private:
    template <class T>
    void visit(const T & e)
    {
        const auto info = exceptionInfo(e);
        ASSERT_TRUE(info.type_info);

        if constexpr (std::is_same_v<std::decay_t<T>, MyException>) {
            EXPECT_EQ(typeid(RuntimeError), *info.type_info);
        }
        else {
            EXPECT_EQ(typeid(T), *info.type_info);
        }

        EXPECT_EQ(
            info.errorText.nonLocalizedString(), QString::fromUtf8(e.what()));
    }

private:
    const ExceptionVariant m_variant;
};

class ExceptionUtilsTest : public testing::TestWithParam<ExceptionVariant>
{};

const std::array gExceptionVariants{
    ExceptionVariant{InvalidArgument{ErrorString{QStringLiteral("message")}}},
    ExceptionVariant{OperationCanceled{}},
    ExceptionVariant{RuntimeError{ErrorString{QStringLiteral("message")}}},
    ExceptionVariant{local_storage::LocalStorageOpenException{
        ErrorString{QStringLiteral("message")}}},
    ExceptionVariant{local_storage::LocalStorageOperationException{
        ErrorString{QStringLiteral("message")}}},
    ExceptionVariant{MyException{QStringLiteral("message")}},
};

INSTANTIATE_TEST_SUITE_P(
    ExceptionUtilsTestInstance, ExceptionUtilsTest,
    testing::ValuesIn(gExceptionVariants));

TEST_P(ExceptionUtilsTest, FetchExceptionInfo)
{
    const auto & testData = GetParam();

    ExceptionVariantVisitor visitor{testData};
    std::visit(visitor, testData);
}

} // namespace quentier::synchronization::tests
