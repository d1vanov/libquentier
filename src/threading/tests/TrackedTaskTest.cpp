/*
 * Copyright 2022 Dmitry Ivanov
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

#include <quentier/threading/TrackedTask.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

// clazy:excludeall=returning-void-expression

namespace quentier::threading::tests {

using testing::Invoke;
using testing::StrictMock;

struct MockCallable
{
    MOCK_METHOD(void, call0, ());
    MOCK_METHOD(void, call2, (bool, int));
};

TEST(TrackedTaskTest, ExpiredObject)
{
    std::weak_ptr<void> selfWeak;
    const TrackedTask trackedTask{selfWeak, [] { ADD_FAILURE(); }};

    trackedTask();
}

TEST(TrackedTaskTest, NonExpiredObject)
{
    const auto object = std::make_shared<int>(1);
    const std::weak_ptr<void> selfWeak = object;

    StrictMock<MockCallable> mockCallable;
    EXPECT_CALL(mockCallable, call0()).Times(1);

    const TrackedTask trackedTask{
        selfWeak, Invoke(&mockCallable, &MockCallable::call0)};

    trackedTask();
}

TEST(TrackedTaskTest, CustomLockableObject)
{
    struct CustomLockable
    {
        [[nodiscard]] bool lock() const noexcept
        {
            return true;
        }
    };

    StrictMock<MockCallable> mockCallable;
    EXPECT_CALL(mockCallable, call0()).Times(1);

    const TrackedTask trackedTask{
        CustomLockable{}, Invoke(&mockCallable, &MockCallable::call0)};

    trackedTask();
}

TEST(TrackedTaskTest, PassArguments)
{
    const auto object = std::make_shared<int>(1);
    const std::weak_ptr<void> selfWeak = object;

    StrictMock<MockCallable> mockCallable;
    EXPECT_CALL(mockCallable, call2(false, 1)).Times(1);

    const TrackedTask callback{
        selfWeak, Invoke(&mockCallable, &MockCallable::call2)};

    callback(false, 1);
}

TEST(TrackedTaskTest, CallMember)
{
    const auto object = std::make_shared<MockCallable>();
    const std::weak_ptr<MockCallable> selfWeak = object;

    EXPECT_CALL(*object, call0()).Times(1);

    const TrackedTask trackedTask{selfWeak, &MockCallable::call0};

    trackedTask();
}

TEST(TrackedTaskTest, CallLink)
{
    const auto object = std::make_shared<MockCallable>();
    const std::weak_ptr<MockCallable> selfWeak = object;

    EXPECT_CALL(*object, call0()).Times(1);

    const TrackedTask trackedTask{
        selfWeak, [&object] { object->call0(); }};

    trackedTask();
}

TEST(TrackedTaskTest, PassArgumentsPack)
{
	const auto object = std::make_shared<MockCallable>();
	const std::weak_ptr<MockCallable> selfWeak = object;

	EXPECT_CALL(*object, call0()).Times(1);

    const TrackedTask trackedTask{
        selfWeak, [&object](auto &&... arguments) {
            object->call0(std::forward<decltype(arguments)>(arguments)...);
        }};

    trackedTask();
}

} // namespace quentier::threading::tests
