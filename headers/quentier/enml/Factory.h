/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include <quentier/enml/Fwd.h>
#include <quentier/utility/Fwd.h>
#include <quentier/utility/Linkage.h>

namespace quentier::enml {

/**
 * @brief factory function for IDecryptedTextCache
 */
[[nodiscard]] QUENTIER_EXPORT IDecryptedTextCachePtr
    createDecryptedTextCache(IEncryptorPtr encryptor);

/**
 * @brief factory function for IENMLTagsConverter
 */
[[nodiscard]] QUENTIER_EXPORT IENMLTagsConverterPtr createEnmlTagsConverter();

/**
 * @brief factory function for IConverter
 * @param enmlTagsConverter instance of IENMLTagsConverter to be used by
 *                          the returned IConverter instance. If nullptr,
 *                          the default implementation of IENMLTagsConverter is
 *                          used.
 */
[[nodiscard]] QUENTIER_EXPORT IConverterPtr
    createConverter(IENMLTagsConverterPtr enmlTagsConverter = nullptr);

} // namespace quentier::enml
