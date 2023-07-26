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

#pragma once

#include <quentier/utility/Linkage.h>

#include <functional>

class QRunnable;

namespace quentier::threading {

/**
 * Create QRunnable from a function - sort of a workaround for Qt < 5.15
 * where QRunnable::create does the same job
 */
[[nodiscard]] auto QUENTIER_EXPORT
    createFunctionRunnable(std::function<void()> function) -> QRunnable *;

} // namespace quentier::threading