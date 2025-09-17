/*
 * Copyright 2016-2025 Dmitry Ivanov
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

#include <QHash>

#include <cstddef>
#include <list>

namespace quentier::utility {

template <
    class Key, class Value,
    class Allocator = std::allocator<std::pair<Key, Value>>>
class LRUCache
{
public:
    explicit LRUCache(const std::size_t maxSize = 100) : m_maxSize{maxSize} {}

    using key_type = Key;
    using mapped_type = Value;
    using allocator_type = Allocator;
    using value_type = std::pair<key_type, mapped_type>;
    using container_type = std::list<value_type, allocator_type>;
    using size_type = typename container_type::size_type;
    using difference_type = typename container_type::difference_type;
    using iterator = typename container_type::iterator;
    using const_iterator = typename container_type::const_iterator;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    using reference = value_type &;
    using const_reference = const value_type &;
    using pointer = typename std::allocator_traits<allocator_type>::pointer;

    using const_pointer =
        typename std::allocator_traits<allocator_type>::const_pointer;

    [[nodiscard]] iterator begin() noexcept
    {
        return m_container.begin();
    }

    [[nodiscard]] const_iterator begin() const noexcept
    {
        return m_container.begin();
    }

    [[nodiscard]] reverse_iterator rbegin() noexcept
    {
        return m_container.rbegin();
    }

    [[nodiscard]] const_reverse_iterator rbegin() const noexcept
    {
        return m_container.rbegin();
    }

    [[nodiscard]] iterator end() noexcept
    {
        return m_container.end();
    }

    [[nodiscard]] const_iterator end() const noexcept
    {
        return m_container.end();
    }

    [[nodiscard]] reverse_iterator rend() noexcept
    {
        return m_container.rend();
    }

    [[nodiscard]] const_reverse_iterator rend() const noexcept
    {
        return m_container.rend();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return m_container.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return m_currentSize;
    }

    [[nodiscard]] std::size_t max_size() const noexcept
    {
        return m_maxSize;
    }

    void clear()
    {
        m_container.clear();
        m_mapper.clear();
        m_currentSize = 0;
    }

    void put(const key_type & key, const mapped_type & value)
    {
        Q_UNUSED(remove(key))

        m_container.push_front(value_type(key, value));
        m_mapper[key] = m_container.begin();
        ++m_currentSize;

        fixupSize();
    }

    [[nodiscard]] const mapped_type * get(const key_type & key) const noexcept
    {
        auto mapperIt = m_mapper.find(key);
        if (mapperIt == m_mapper.end()) {
            return nullptr;
        }

        auto it = mapperIt.value();
        if (it == m_container.end()) {
            return nullptr;
        }

        m_container.splice(m_container.begin(), m_container, it);
        mapperIt.value() = m_container.begin();
        return &(mapperIt.value()->second);
    }

    [[nodiscard]] bool exists(const key_type & key) const noexcept
    {
        const auto mapperIt = m_mapper.find(key);
        if (mapperIt == m_mapper.end()) {
            return false;
        }

        const auto it = mapperIt.value();
        return (it != m_container.end());
    }

    bool remove(const key_type & key) noexcept
    {
        const auto mapperIt = m_mapper.find(key);
        if (mapperIt == m_mapper.end()) {
            return false;
        }

        const auto it = mapperIt.value();
        Q_UNUSED(m_container.erase(it))
        Q_UNUSED(m_mapper.erase(mapperIt))

        if (m_currentSize != 0) {
            --m_currentSize;
        }

        return true;
    }

    void setMaxSize(const std::size_t maxSize)
    {
        if (maxSize >= m_maxSize) {
            m_maxSize = maxSize;
            return;
        }

        std::size_t diff = m_maxSize - maxSize;
        for (std::size_t i = 0; (i < diff) && !m_container.empty(); ++i) {
            auto lastIt = m_container.end();
            --lastIt;

            const key_type & lastElementKey = lastIt->first;
            Q_UNUSED(m_mapper.remove(lastElementKey))
            Q_UNUSED(m_container.erase(lastIt))

            if (m_currentSize != 0) {
                --m_currentSize;
            }
        }
    }

private:
    void fixupSize()
    {
        if (m_currentSize <= m_maxSize) {
            return;
        }

        if (Q_UNLIKELY(m_container.empty())) {
            return;
        }

        auto lastIt = m_container.end();
        --lastIt;

        const key_type & lastElementKey = lastIt->first;

        Q_UNUSED(m_mapper.remove(lastElementKey))
        Q_UNUSED(m_container.erase(lastIt))

        if (m_currentSize != 0) {
            --m_currentSize;
        }
    }

private:
    mutable container_type m_container;
    std::size_t m_currentSize = 0;
    std::size_t m_maxSize;

    mutable QHash<Key, iterator> m_mapper;
};

} // namespace quentier::utility

// TODO: remove after migration to namespaced version in Quentier
namespace quentier {

template <
    class Key, class Value,
    class Allocator = std::allocator<std::pair<Key, Value>>>
using LRUCache = utility::LRUCache<Key, Value, Allocator>;

} // namespace quentier
