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

#include <array>
#include <cstdint>
#include <memory>
#include <new>

namespace quentier::utility {

template <typename T, typename... Args>
[[nodiscard]] std::shared_ptr<T> makeSelfShared(Args &&... args)
{
	class SharedHolder : public std::enable_shared_from_this<SharedHolder>
	{
    public:
		std::shared_ptr<T> construct(Args &&... args)
		{
			auto ptr = std::launder(reinterpret_cast<T*>(m_buffer.data()));
			auto alias = std::shared_ptr<T>(this->shared_from_this(), ptr);
			::new (ptr) T{alias, std::forward<Args>(args)...};
			m_ptr = std::launder(reinterpret_cast<T*>(m_buffer.data()));
			return alias;
		}

		SharedHolder() = default;

		~SharedHolder()
		{
			if (m_ptr)
			{
				std::destroy_at(m_ptr);
			}
		}

		SharedHolder(const SharedHolder &) = delete;
		SharedHolder(SharedHolder &&) = delete;

		SharedHolder & operator=(const SharedHolder &) = delete;
		SharedHolder & operator=(SharedHolder &&) = delete;

    private:
		alignas(T) std::array<std::uint8_t, sizeof(T)> m_buffer;
		T * m_ptr = nullptr;
	};

	auto holder = std::make_shared<SharedHolder>();
	return holder->construct(std::forward<Args>(args)...);
}

} // namespace quentier::utility
