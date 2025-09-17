/*
 * Copyright 2018-2025 Dmitry Ivanov
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

#include "LRUCacheTests.h"

#include <quentier/utility/LRUCache.hpp>

#include <cstddef>
#include <cstdint>

namespace quentier::utility::test {

bool testEmptyLRUCacheConsistency(QString & error)
{
    const quint32 maxSize = 5;
    LRUCache<QString, int> cache(maxSize);

    if (Q_UNLIKELY(!cache.empty())) {
        error = QStringLiteral(
            "Empty LRUCache's empty method unexpectedly returns true");

        return false;
    }

    if (Q_UNLIKELY(!cache.empty())) {
        error =
            QStringLiteral("Empty LRUCache's size method returns non-zero: ") +
            QString::number(cache.size());

        return false;
    }

    if (Q_UNLIKELY(cache.max_size() != maxSize)) {
        error = QStringLiteral("Empty LRUCache's max_size method returns ") +
            QString::number(cache.max_size()) +
            QStringLiteral(" while expected ") + QString::number(maxSize);

        return false;
    }

    if (Q_UNLIKELY(cache.begin() != cache.end())) {
        error = QStringLiteral(
            "Empty LRUCache's begin method returns iterator "
            "not equal to its end method");

        return false;
    }

    if (Q_UNLIKELY(cache.rbegin() != cache.rend())) {
        error = QStringLiteral(
            "Empty LRUCache's rbegin method returns iterator "
            "not equal to its rend method");

        return false;
    }

    return true;
}

bool testNonEmptyLRUCacheConsistency(QString & error)
{
    const quint32 maxSize = 5;
    LRUCache<QString, int> cache(maxSize);

    const QString firstItemName = QStringLiteral("My first item");
    const QString secondItemName = QStringLiteral("My second item");
    const QString thirdItemName = QStringLiteral("My third item");

    if (Q_UNLIKELY( // NOLINT
            (cache.get(firstItemName) != nullptr) ||
            (cache.get(secondItemName) != nullptr) ||
            (cache.get(thirdItemName) != nullptr)))
    {
        error = QStringLiteral(
            "Empty LRUCache's get method returned non-null pointer to item");

        return false;
    }

    cache.put(firstItemName, 1);
    cache.put(secondItemName, 2);
    cache.put(thirdItemName, 3);

    if (Q_UNLIKELY(cache.empty())) {
        error = QStringLiteral(
            "LRUCache's empty method returns true after "
            "several items were added to it");

        return false;
    }

    if (Q_UNLIKELY(cache.size() != 3)) {
        error = QStringLiteral("LRUCache's size method returned unexpected ") +
            QStringLiteral("value (") + QString::number(cache.size()) +
            QStringLiteral(") instead of the expected one (3)");

        return false;
    }

    if (Q_UNLIKELY(cache.max_size() != maxSize)) {
        error = QStringLiteral("LRUCache's max_size method returned ") +
            QStringLiteral("unexpected value (") +
            QString::number(cache.max_size()) +
            QStringLiteral(") instead of the expected one (5)");

        return false;
    }

    const int * firstItemValue = cache.get(firstItemName);
    const int * secondItemValue = cache.get(secondItemName);
    const int * thirdItemValue = cache.get(thirdItemName);

    if (Q_UNLIKELY( // NOLINT
            (firstItemValue == nullptr) || (secondItemValue == nullptr) ||
            (thirdItemValue == nullptr)))
    {
        error = QStringLiteral(
            "One or some of items inserted into LRU cache were not found in "
            "it");

        return false;
    }

    if (Q_UNLIKELY( // NOLINT
            (*firstItemValue != 1) || (*secondItemValue != 2) ||
            (*thirdItemValue != 3)))
    {
        error = QStringLiteral("LRUCache returns wrong items from get method");
        return false;
    }

    if (Q_UNLIKELY(cache.get(QStringLiteral("Nonexisting item")) != nullptr)) {
        error = QStringLiteral(
            "LRUCache's get method returns non-null pointer for nonexisting "
            "item");

        return false;
    }

    if (Q_UNLIKELY(cache.begin() == cache.end())) {
        error = QStringLiteral(
            "LRUCache's begin and end iterators are equal "
            "even though the cache is not empty");

        return false;
    }

    if (Q_UNLIKELY(cache.rbegin() == cache.rend())) {
        error = QStringLiteral(
            "LRUCache's rbegin and rend iterators are equal "
            "even though the cache is not empty");

        return false;
    }

    cache.clear();

    if (Q_UNLIKELY(!cache.empty())) {
        error = QStringLiteral("LRUCache is not empty after calling clear");
        return false;
    }

    if (Q_UNLIKELY(!cache.empty())) {
        error = QStringLiteral(
            "LRUCache's size method returns non-zero value on cleared cache");

        return false;
    }

    if (Q_UNLIKELY(cache.max_size() != maxSize)) {
        error = QStringLiteral("LRUCache's max_size method returned ") +
            QStringLiteral("unexpected value (") +
            QString::number(cache.max_size()) +
            QStringLiteral(") instead of the expected one (5)");

        return false;
    }

    if (Q_UNLIKELY(cache.begin() != cache.end())) {
        error = QStringLiteral(
            "Empty LRUCache's begin method returns iterator "
            "not equal to its end method");

        return false;
    }

    if (Q_UNLIKELY(cache.rbegin() != cache.rend())) {
        error = QStringLiteral(
            "Empty LRUCache's rbegin method returns iterator "
            "not equal to its rend method");

        return false;
    }

    return true;
}

bool testRemovalFromLRUCache(QString & error)
{
    const quint32 maxSize = 5;
    LRUCache<QString, int> cache(maxSize);

    const QString firstItemName = QStringLiteral("My first item");
    const QString secondItemName = QStringLiteral("My second item");
    const QString thirdItemName = QStringLiteral("My third item");

    cache.put(firstItemName, 1);
    cache.put(secondItemName, 2);
    cache.put(thirdItemName, 3);

    if (Q_UNLIKELY(!cache.remove(firstItemName))) {
        error = QStringLiteral(
            "LRUCache's remove method returned false "
            "on attempt to delete entry definitely existing in the cache");
        return false;
    }

    if (Q_UNLIKELY(cache.empty())) {
        error = QStringLiteral(
            "LRUCache's empty method returns true after several "
            "items were added to it and only one was removed");

        return false;
    }

    if (Q_UNLIKELY(cache.size() != 2)) {
        error = QStringLiteral("LRUCache's size method returned unexpected ") +
            QStringLiteral("value (") + QString::number(cache.size()) +
            QStringLiteral(") instead of the expected one (2)");

        return false;
    }

    if (Q_UNLIKELY(cache.max_size() != maxSize)) {
        error = QStringLiteral("LRUCache's max_size method returned ") +
            QStringLiteral("unexpected value (") +
            QString::number(cache.max_size()) +
            QStringLiteral(") instead of the expected one (5)");

        return false;
    }

    const int * firstItemValue = cache.get(firstItemName);
    const int * secondItemValue = cache.get(secondItemName);
    const int * thirdItemValue = cache.get(thirdItemName);

    if (Q_UNLIKELY(firstItemValue != nullptr)) {
        error = QStringLiteral(
            "LRUCache's get method returned non-null pointer "
            "for item removed from the cache");

        return false;
    }

    if (Q_UNLIKELY( // NOLINT
            (secondItemValue == nullptr) || (thirdItemValue == nullptr)))
    {
        error = QStringLiteral(
            "One or some of items inserted into LRU cache and "
            "not removed from it were not found in it");

        return false;
    }

    if (Q_UNLIKELY(cache.get(QStringLiteral("Nonexisting item")) != nullptr)) {
        error = QStringLiteral(
            "LRUCache's get method returns non-null pointer "
            "for nonexisting item");

        return false;
    }

    if (Q_UNLIKELY( // NOLINT
            (*secondItemValue != 2) || (*thirdItemValue != 3)))
    {
        error = QStringLiteral(
            "LRUCache returns wrong items from get method "
            "after one item removal");

        return false;
    }

    if (Q_UNLIKELY(!cache.remove(secondItemName))) {
        error = QStringLiteral(
            "LRUCache's remove method returned false "
            "on attempt to delete entry definitely existing in the cache");
        return false;
    }

    if (Q_UNLIKELY(cache.empty())) {
        error = QStringLiteral(
            "LRUCache's empty method returns true after several "
            "items were added to it and only some of them were "
            "removed");

        return false;
    }

    if (Q_UNLIKELY(cache.size() != 1)) {
        error = QStringLiteral(
                    "LRUCache's size method returned unexpected "
                    "value (") +
            QString::number(cache.size()) +
            QStringLiteral(") instead of the expected one (1)");

        return false;
    }

    if (Q_UNLIKELY(cache.max_size() != maxSize)) {
        error = QStringLiteral(
                    "LRUCache's max_size method returned unexpected "
                    "value (") +
            QString::number(cache.max_size()) +
            QStringLiteral(") instead of the expected one (5)");

        return false;
    }

    firstItemValue = cache.get(firstItemName);
    secondItemValue = cache.get(secondItemName);
    thirdItemValue = cache.get(thirdItemName);

    if (Q_UNLIKELY(firstItemValue != nullptr)) {
        error = QStringLiteral(
            "LRUCache's get method returned non-null pointer "
            "for item removed from the cache");

        return false;
    }

    if (Q_UNLIKELY(secondItemValue != nullptr)) {
        error = QStringLiteral(
            "LRUCache's get method returned non-null pointer "
            "for item removed from the cache");

        return false;
    }

    if (Q_UNLIKELY(thirdItemValue == nullptr)) {
        error = QStringLiteral(
            "LRUCache's get method returned null pointer for "
            "the single item which should have been left "
            "in the cache");

        return false;
    }

    if (Q_UNLIKELY(*thirdItemValue != 3)) {
        error = QStringLiteral(
            "LRUCache returns wrong item from get method for "
            "the single item left in the cache");

        return false;
    }

    if (Q_UNLIKELY(!cache.remove(thirdItemName))) {
        error = QStringLiteral(
            "LRUCache's remove method returned false "
            "on attempt to delete entry definitely existing in the cache");
        return false;
    }

    if (Q_UNLIKELY(!cache.empty())) {
        error =
            QStringLiteral("LRUCache is not empty after removing all items");

        return false;
    }

    if (Q_UNLIKELY(!cache.empty())) {
        error = QStringLiteral(
            "LRUCache's size method returns non-zero value "
            "on cache all items of which were removed");

        return false;
    }

    if (Q_UNLIKELY(cache.max_size() != maxSize)) {
        error = QStringLiteral(
                    "LRUCache's max_size method returned unexpected "
                    "value (") +
            QString::number(cache.max_size()) +
            QStringLiteral(") instead of the expected one (5)");

        return false;
    }

    if (Q_UNLIKELY(cache.begin() != cache.end())) {
        error = QStringLiteral(
            "Empty LRUCache's begin method returns iterator "
            "not equal to its end method");

        return false;
    }

    if (Q_UNLIKELY(cache.rbegin() != cache.rend())) {
        error = QStringLiteral(
            "Empty LRUCache's rbegin method returns iterator "
            "not equal to its rend method");

        return false;
    }

    return true;
}

bool testLRUCacheReverseIterators(QString & error)
{
    constexpr std::size_t maxSize = 5;
    LRUCache<QString, int> cache(maxSize);

    const QString firstItemName = QStringLiteral("My first item");
    const QString secondItemName = QStringLiteral("My second item");
    const QString thirdItemName = QStringLiteral("My third item");

    cache.put(firstItemName, 1);
    cache.put(secondItemName, 2);
    cache.put(thirdItemName, 3);

    if (Q_UNLIKELY(&(*cache.rbegin()) != &(*(--cache.end())))) {
        error =
            QStringLiteral("LRUCache's rbegin doesn't point to the right item");
        return false;
    }

    if (Q_UNLIKELY(&(*(--cache.rend())) != &(*cache.begin()))) {
        error =
            QStringLiteral("LRUCache's rend doesn't point to the right item");
        return false;
    }

    return true;
}

bool testItemsAdditionToLRUCacheBeforeReachingMaxSize(QString & error)
{
    constexpr std::size_t maxSize = 5;
    LRUCache<QString, int> cache(maxSize);

    if (Q_UNLIKELY(!cache.empty())) {
        error = QStringLiteral(
            "Empty LRUCache's empty method unexpectedly returns true");
        return false;
    }

    if (Q_UNLIKELY(!cache.empty())) {
        error =
            QStringLiteral("Empty LRUCache's size method returns non-zero: ") +
            QString::number(cache.size());

        return false;
    }

    if (Q_UNLIKELY(cache.max_size() != maxSize)) {
        error = QStringLiteral("Empty LRUCache's max_size method returns ") +
            QString::number(cache.max_size()) +
            QStringLiteral(" while expected ") + QString::number(maxSize);

        return false;
    }

    const QString firstItemName = QStringLiteral("My first item");
    const QString secondItemName = QStringLiteral("My second item");
    const QString thirdItemName = QStringLiteral("My third item");

    cache.put(firstItemName, 1);

    if (Q_UNLIKELY(cache.empty())) {
        error = QStringLiteral(
            "LRUCache's empty method returns true after one item was added "
            "to it");

        return false;
    }

    if (Q_UNLIKELY(cache.size() != 1)) {
        error = QStringLiteral(
                    "LRUCache's size method returned unexpected "
                    "value (") +
            QString::number(cache.size()) +
            QStringLiteral(") instead of the expected one (1)");

        return false;
    }

    if (Q_UNLIKELY(cache.max_size() != maxSize)) {
        error = QStringLiteral(
                    "LRUCache's max_size method returned unexpected "
                    "value (") +
            QString::number(cache.max_size()) +
            QStringLiteral(") instead of the expected one (5)");

        return false;
    }

    if (Q_UNLIKELY(cache.begin()->second != 1)) {
        error = QStringLiteral(
            "The most recently added item wasn't put into the beginning of "
            "the cache");

        return false;
    }

    cache.put(secondItemName, 2);

    if (Q_UNLIKELY(cache.empty())) {
        error = QStringLiteral(
            "LRUCache's empty method returns true after two items were added "
            "to it");

        return false;
    }

    if (Q_UNLIKELY(cache.size() != 2)) {
        error = QStringLiteral(
                    "LRUCache's size method returned unexpected value (") +
            QString::number(cache.size()) +
            QStringLiteral(") instead of the expected one (2)");
        return false;
    }

    if (Q_UNLIKELY(cache.max_size() != maxSize)) {
        error = QStringLiteral(
                    "LRUCache's max_size method returned unexpected "
                    "value (") +
            QString::number(cache.max_size()) +
            QStringLiteral(") instead of the expected one (5)");

        return false;
    }

    if (Q_UNLIKELY(cache.begin()->second != 2)) {
        error = QStringLiteral(
            "The most recently added item wasn't put into the beginning of "
            "the cache");

        return false;
    }

    cache.put(thirdItemName, 3);

    if (Q_UNLIKELY(cache.empty())) {
        error = QStringLiteral(
            "LRUCache's empty method returns true after three items were "
            "added to it");

        return false;
    }

    if (Q_UNLIKELY(cache.size() != 3)) {
        error = QStringLiteral(
                    "LRUCache's size method returned unexpected "
                    "value (") +
            QString::number(cache.size()) +
            QStringLiteral(") instead of the expected one (3)");

        return false;
    }

    if (Q_UNLIKELY(cache.max_size() != maxSize)) {
        error = QStringLiteral(
                    "LRUCache's max_size method returned unexpected "
                    "value (") +
            QString::number(cache.max_size()) +
            QStringLiteral(") instead of the expected one (5)");

        return false;
    }

    if (Q_UNLIKELY(cache.begin()->second != 3)) {
        error = QStringLiteral(
            "The most recently added item wasn't put into the beginning of "
            "the cache");

        return false;
    }

    const int * secondItemValue = cache.get(secondItemName);
    if (Q_UNLIKELY(secondItemValue == nullptr)) {
        error = QStringLiteral(
            "LRUCache's get method returned null pointer to item which was "
            "added to it before");

        return false;
    }

    if (Q_UNLIKELY(*secondItemValue != 2)) {
        error = QStringLiteral("LRUCache returned wrong item from get method");
        return false;
    }

    if (Q_UNLIKELY(cache.begin()->second != *secondItemValue)) {
        error = QStringLiteral(
            "The most recently accessed item wasn't moved "
            "to the beginning of the cache");

        return false;
    }

    const int * firstItemValue = cache.get(firstItemName);
    if (Q_UNLIKELY(firstItemValue == nullptr)) {
        error = QStringLiteral(
            "LRUCache's get method returned null pointer to "
            "item which was added to it before");

        return false;
    }

    if (Q_UNLIKELY(*firstItemValue != 1)) {
        error = QStringLiteral("LRUCache returned wrong item from get method");
        return false;
    }

    if (Q_UNLIKELY(cache.begin()->second != *firstItemValue)) {
        error = QStringLiteral(
            "The most recently accessed item wasn't moved "
            "to the beginning of the cache");

        return false;
    }

    return true;
}

bool testItemsAdditionToLRUCacheAfterReachingMaxSize(QString & error)
{
    constexpr std::size_t maxSize = 5;
    LRUCache<QString, int> cache(maxSize);

    if (Q_UNLIKELY(!cache.empty())) {
        error = QStringLiteral(
            "Empty LRUCache's empty method unexpectedly returns true");
        return false;
    }

    if (Q_UNLIKELY(!cache.empty())) {
        error =
            QStringLiteral("Empty LRUCache's size method returns non-zero: ") +
            QString::number(cache.size());

        return false;
    }

    if (Q_UNLIKELY(cache.max_size() != maxSize)) {
        error = QStringLiteral("Empty LRUCache's max_size method returns ") +
            QString::number(cache.max_size()) +
            QStringLiteral(" while expected ") + QString::number(maxSize);

        return false;
    }

    const QString firstItemName = QStringLiteral("My first item");
    const QString secondItemName = QStringLiteral("My second item");
    const QString thirdItemName = QStringLiteral("My third item");
    const QString fourthItemName = QStringLiteral("My fourth item");
    const QString fifthItemName = QStringLiteral("My fifth item");
    const QString sixthItemName = QStringLiteral("My sixth item");
    const QString seventhItemName = QStringLiteral("My seventh item");

    cache.put(firstItemName, 1);
    cache.put(secondItemName, 2);
    cache.put(thirdItemName, 3);
    cache.put(fourthItemName, 4);
    cache.put(fifthItemName, 5);

    if (Q_UNLIKELY(cache.empty())) {
        error = QStringLiteral(
            "LRUCache is empty after adding several items to it");
        return false;
    }

    if (Q_UNLIKELY(cache.size() != 5)) {
        error = QStringLiteral(
                    "LRUCache's size method returned unexpected "
                    "value (") +
            QString::number(cache.size()) +
            QStringLiteral(") instead of the expected one (5)");

        return false;
    }

    if (Q_UNLIKELY(cache.max_size() != maxSize)) {
        error = QStringLiteral(
                    "LRUCache's max_size method returned unexpected "
                    "value (") +
            QString::number(cache.max_size()) +
            QStringLiteral(") instead of the expected one (5)");

        return false;
    }

    cache.put(sixthItemName, 6);

    if (Q_UNLIKELY(cache.empty())) {
        error = QStringLiteral(
            "LRUCache is empty after adding several items to it");
        return false;
    }

    if (Q_UNLIKELY(cache.size() != maxSize)) {
        error = QStringLiteral(
                    "LRUCache's size method returned unexpected "
                    "value (") +
            QString::number(cache.size()) +
            QStringLiteral(") instead of the expected one (5)");

        return false;
    }

    if (Q_UNLIKELY(cache.max_size() != maxSize)) {
        error = QStringLiteral(
                    "LRUCache's max_size method returned unexpected "
                    "value (") +
            QString::number(cache.max_size()) +
            QStringLiteral(") instead of the expected one (5)");

        return false;
    }

    if (Q_UNLIKELY(cache.begin()->second != 6)) {
        error = QStringLiteral(
            "The most recently accessed item wasn't moved "
            "to the beginning of the cache");

        return false;
    }

    const int * firstItemValue = cache.get(firstItemName);
    if (Q_UNLIKELY(firstItemValue != nullptr)) {
        error = QStringLiteral(
            "LRUCache's get method returned non-null pointer "
            "for item which should have been automatically "
            "removed from the cache");

        return false;
    }

    const int * secondItemValue = cache.get(secondItemName);
    const int * thirdItemValue = cache.get(thirdItemName);
    const int * fourthItemValue = cache.get(fourthItemName);
    const int * fifthItemValue = cache.get(fifthItemName);
    const int * sixthItemValue = cache.get(sixthItemName);

    if (Q_UNLIKELY( // NOLINT
            (secondItemValue == nullptr) || (thirdItemValue == nullptr) ||
            (fourthItemValue == nullptr) || (fifthItemValue == nullptr) ||
            (sixthItemValue == nullptr)))
    {
        error = QStringLiteral(
            "One or some of items inserted into LRU cache and "
            "not removed from it were not found in it");

        return false;
    }

    if (Q_UNLIKELY( // NOLINT
            (*secondItemValue != 2) || (*thirdItemValue != 3) ||
            (*fourthItemValue != 4) || (*fifthItemValue != 5) ||
            (*sixthItemValue != 6)))
    {
        error = QStringLiteral("LRUCache returns wrong items from get methods");
        return false;
    }

    cache.put(seventhItemName, 7);

    if (Q_UNLIKELY(cache.empty())) {
        error = QStringLiteral(
            "LRUCache is empty after adding several items to it");
        return false;
    }

    if (Q_UNLIKELY(cache.size() != maxSize)) {
        error = QStringLiteral(
                    "LRUCache's size method returned unexpected "
                    "value (") +
            QString::number(cache.size()) +
            QStringLiteral(") instead of the expected one (5)");

        return false;
    }

    if (Q_UNLIKELY(cache.max_size() != maxSize)) {
        error = QStringLiteral(
                    "LRUCache's max_size method returned "
                    "unexpected value (") +
            QString::number(cache.max_size()) +
            QStringLiteral(") instead of the expected one (5)");

        return false;
    }

    if (Q_UNLIKELY(cache.begin()->second != 7)) {
        error = QStringLiteral(
            "The most recently accessed item wasn't moved "
            "to the beginning of the cache");

        return false;
    }

    firstItemValue = cache.get(firstItemName);
    secondItemValue = cache.get(secondItemName);
    thirdItemValue = cache.get(thirdItemName);
    fourthItemValue = cache.get(fourthItemName);
    fifthItemValue = cache.get(fifthItemName);
    sixthItemValue = cache.get(sixthItemName);
    const int * seventhItemValue = cache.get(seventhItemName);

    if (Q_UNLIKELY( // NOLINT
            (firstItemValue != nullptr) || (secondItemValue != nullptr)))
    {
        error = QStringLiteral(
            "LRUCache's get method returned non-null pointer for items which "
            "should have been automatically removed from the cache");

        return false;
    }

    if (Q_UNLIKELY( // NOLINT
            (thirdItemValue == nullptr) || (fourthItemValue == nullptr) ||
            (fifthItemValue == nullptr) || (sixthItemValue == nullptr) ||
            (seventhItemValue == nullptr)))
    {
        error = QStringLiteral(
            "One or some of items inserted into LRU cache and not removed "
            "from it were not found in it");

        return false;
    }

    if (Q_UNLIKELY( // NOLINT
            (*thirdItemValue != 3) || (*fourthItemValue != 4) ||
            (*fifthItemValue != 5) || (*sixthItemValue != 6) ||
            (*seventhItemValue != 7)))
    {
        error = QStringLiteral("LRUCache returns wrong items from get methoda");
        return false;
    }

    return true;
}

} // namespace quentier::utility::test
