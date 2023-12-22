/*
 * Copyright 2016-2023 Dmitry Ivanov
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

#include <memory>

namespace quentier::enml {

class IConverter;
using IConverterPtr = std::shared_ptr<IConverter>;

class IDecryptedTextCache;
using IDecryptedTextCachePtr = std::shared_ptr<IDecryptedTextCache>;

class IENMLTagsConverter;
using IENMLTagsConverterPtr = std::shared_ptr<IENMLTagsConverter>;

struct IHtmlData;
using IHtmlDataPtr = std::shared_ptr<IHtmlData>;

} // namespace quentier::enml
