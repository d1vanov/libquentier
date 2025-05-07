/*
 * Copyright 2024-2025 Dmitry Ivanov
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

#include <quentier/utility/Fwd.h>
#include <quentier/utility/Linkage.h>

namespace quentier::utility {

/**
 * Factory function creating IEnctyptor instance using OpenSSL algorithms for
 * data encryption and decryption.
 */
[[nodiscard]] QUENTIER_EXPORT IEncryptorPtr createOpenSslEncryptor();

} // namespace quentier::utility
