/*
 * Copyright 2021 Dmitry Ivanov
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

#include <quentier/utility/Threading.h>

#include <QRunnable>
#include <QtGlobal>

namespace quentier::utility {

class Q_DECL_HIDDEN FunctionRunnable final: public QRunnable
{
public:
    explicit FunctionRunnable(std::function<void()> function)
        : m_function{std::move(function)}
    {
        Q_ASSERT(m_function);
    }

    void run() override
    {
        m_function();
    }

private:
    std::function<void()> m_function;
};

[[nodiscard]] QRunnable * createFunctionRunnable(std::function<void()> function)
{
    return new FunctionRunnable(std::move(function));
}

} // namespace quentier::utility
