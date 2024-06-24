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

#include <synchronization/types/SerializationUtils.h>

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

class ExceptionVariantSerializeVisitor
{
public:
    explicit ExceptionVariantSerializeVisitor(ExceptionVariant v) :
        m_variant{std::move(v)}
    {}

    [[nodiscard]] QJsonObject operator()(const InvalidArgument & e)
    {
        return visit(e);
    }

    [[nodiscard]] QJsonObject operator()(const OperationCanceled & e)
    {
        return visit(e);
    }

    [[nodiscard]] QJsonObject operator()(const RuntimeError & e)
    {
        return visit(e);
    }

    [[nodiscard]] QJsonObject operator()(
        const local_storage::LocalStorageOpenException & e)
    {
        return visit(e);
    }

    [[nodiscard]] QJsonObject operator()(
        const local_storage::LocalStorageOperationException & e)
    {
        return visit(e);
    }

    [[nodiscard]] QJsonObject operator()(const MyException & e)
    {
        return visit(e);
    }

private:
    template <class T>
    [[nodiscard]] QJsonObject visit(const T & e)
    {
        return serializeException(e);
    }

private:
    const ExceptionVariant m_variant;
};

class ExceptionVariantDeserializeVisitor
{
public:
    explicit ExceptionVariantDeserializeVisitor(std::shared_ptr<QException> e) :
        m_exception{std::move(e)}
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
        ASSERT_TRUE(m_exception);

        using ExpectedExceptionType = std::conditional_t<
            std::is_same_v<std::decay_t<T>, MyException>, QException, T>;

        try {
            m_exception->raise();
        }
        catch (const ExpectedExceptionType & exc) {
            EXPECT_EQ(
                QString::fromUtf8(exc.what()), QString::fromUtf8(e.what()));
            return;
        }
        catch (...) {
            EXPECT_TRUE(false) << "Failed to catch the expected exception";
        }
    }

private:
    const std::shared_ptr<QException> m_exception;
};

class SerializationUtilsTest : public testing::TestWithParam<ExceptionVariant>
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
    SerializationUtilsTestInstance, SerializationUtilsTest,
    testing::ValuesIn(gExceptionVariants));

TEST_P(SerializationUtilsTest, SerializeAndDeserializeException)
{
    const auto & testData = GetParam();

    ExceptionVariantSerializeVisitor serializeVisitor{testData};
    const auto jsonObject = std::visit(serializeVisitor, testData);

    const auto deserializedException = deserializeException(jsonObject);
    ASSERT_TRUE(deserializedException);

    ExceptionVariantDeserializeVisitor deserializeVisitor{
        deserializedException};

    std::visit(deserializeVisitor, testData);
}

} // namespace quentier::synchronization::tests
