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

#pragma once

#include <quentier/enml/IENMLTagsConverter.h>

namespace quentier::enml {

class ENMLTagsConverter : public IENMLTagsConverter
{
public: // IENMLTagsConverter
    [[nodiscard]] QString convertEnToDo(
        bool checked, quint32 index) const override;

    [[nodiscard]] QString convertEncryptedText(
        const QString & encryptedText, const QString & hint,
        utility::IEncryptor::Cipher cipher, quint32 index) const override;

    [[nodiscard]] QString convertDecryptedText(
        const QString & decryptedText, const QString & encryptedText,
        const QString & hint, utility::IEncryptor::Cipher cipher,
        quint32 index) const override;

    [[nodiscard]] Result<QString, ErrorString> convertResource(
        const qevercloud::Resource & resource) const override;
};

} // namespace quentier::enml
