/*
 * Copyright 2023-2025 Dmitry Ivanov
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

#include <quentier/enml/Factory.h>

#include <utility/Encryptor.h>

#include "Converter.h"
#include "DecryptedTextCache.h"
#include "ENMLTagsConverter.h"

namespace quentier::enml {

IDecryptedTextCachePtr createDecryptedTextCache(IEncryptorPtr encryptor)
{
    if (!encryptor) {
        encryptor = std::make_shared<Encryptor>();
    }

    return std::make_shared<DecryptedTextCache>(std::move(encryptor));
}

IENMLTagsConverterPtr createEnmlTagsConverter()
{
    return std::make_shared<ENMLTagsConverter>();
}

IConverterPtr createConverter(IENMLTagsConverterPtr enmlTagsConverter)
{
    if (!enmlTagsConverter) {
        enmlTagsConverter = createEnmlTagsConverter();
    }

    return std::make_shared<Converter>(std::move(enmlTagsConverter));
}

} // namespace quentier::enml
