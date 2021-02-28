/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_UTILITY_LRU_CACHE_HPP
#define LIB_QUENTIER_UTILITY_LRU_CACHE_HPP

#include <QHash>

#include <cstddef>
#include <list>

namespace quentier {

template <
    class Key, class Value,
    class Allocator = std::allocator<std::pair<Key, Value>>>
class LRUCache
{
public:
    LRUCache(const size_t maxSize = 100) :
        m_container(), m_currentSize(0), m_maxSize(maxSize), m_mapper()
    {}

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

    iterator begin()
    {
        return m_container.begin();
    }
    const_iterator begin() const
    {
        return m_container.begin();
    }

    reverse_iterator rbegin()
    {
        return m_container.rbegin();
    }
    const_reverse_iterator rbegin() const
    {
        return m_container.rbegin();
    }

    iterator end()
    {
        return m_container.end();
    }
    const_iterator end() const
    {
        return m_container.end();
    }

    reverse_iterator rend()
    {
        return m_container.rend();
    }
    const_reverse_iterator rend() const
    {
        return m_container.rend();
    }

    bool empty() const
    {
        return m_container.empty();
    }
    size_t size() const
    {
        return m_currentSize;
    }
    size_t max_size() const
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

    const mapped_type * get(const key_type & key) const
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

    bool exists(const key_type & key)
    {
        auto mapperIt = m_mapper.find(key);
        if (mapperIt == m_mapper.end()) {
            return false;
        }

        auto it = mapperIt.value();
        return (it != m_container.end());
    }

    bool remove(const key_type & key)
    {
        auto mapperIt = m_mapper.find(key);
        if (mapperIt == m_mapper.end()) {
            return false;
        }

        auto it = mapperIt.value();
        Q_UNUSED(m_container.erase(it))
        Q_UNUSED(m_mapper.erase(mapperIt))

        if (m_currentSize != 0) {
            --m_currentSize;
        }

        return true;
    }

    void setMaxSize(const size_t maxSize)
    {
        if (maxSize >= m_maxSize) {
            m_maxSize = maxSize;
            return;
        }

        size_t diff = m_maxSize - maxSize;
        for (size_t i = 0; (i < diff) && !m_container.empty(); ++i) {
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
    size_t m_currentSize;
    size_t m_maxSize;

    mutable QHash<Key, iterator> m_mapper;
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_LRU_CACHE_HPP
