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

#include <quentier/types/ErrorString.h>
#include <quentier/types/Result.h>
#include <quentier/utility/Linkage.h>

#include <qevercloud/types/Fwd.h>

#include <QString>
#include <QtGlobal>

#include <cstddef>

namespace quentier::enml {

/**
 * @brief The IENMLTagsConverter interfaces provides methods which convert
 * Evernote-specific markup tags such as en-crypt, en-media etc. into their
 * counterparts which should be used in the HTML representation of note content.
 */
class QUENTIER_EXPORT IENMLTagsConverter
{
public:
    virtual ~IENMLTagsConverter();

    /**
     * Converts en-todo tag into its HTML counterpart
     * @param checked indicates whether this todo is checked or not
     * @param index index of particular en-todo tag within the note content
     *              so that different todo tags can be differentiated
     * @return HTML representation of en-todo tag
     */
    [[nodiscard]] virtual QString convertEnToDo(
        bool checked, quint32 index) const = 0;

    /**
     * Converts en-crypt tag into its HTML counterpart
     * @param encryptedText encrypted text contained within en-crypt tag
     * @param hint hint to be displayed when user tries to decrypt the text
     * @param cipher cipher used to ecrypt the text
     * @param keyLength length of the key used to encrypt the text
     * @param index index of particular en-crypt tag within the note content
     *              so that different en-crypt tags can be differentiated
     * @return HTML representation of en-crypt tag
     */
    [[nodiscard]] virtual QString convertEncryptedText(
        const QString & encryptedText, const QString & hint,
        const QString & cipher, std::size_t keyLength, quint32 index) const = 0;

    /**
     * Converts already decrypted en-crypt tag into its HTML counterpart
     * @param decryptedText decrypted text from en-crypt tag
     * @param encryptedText encrypted text contained within en-crypt tag
     * @param hint hint to be displayed when user tries to decrypt the text
     * @param cipher cipher used to ecrypt the text
     * @param keyLength length of the key used to encrypt the text
     * @param index index of particular en-crypt tag within the note content
     *              so that different en-crypt tags can be differentiated
     * @return HTML representation of decrypted en-crypt tag
     */
    [[nodiscard]] virtual QString convertDecryptedText(
        const QString & decryptedText, const QString & encryptedText,
        const QString & hint, const QString & cipher, std::size_t keyLength,
        quint32 index) const = 0;

    /**
     * Converts en-media tag representing a resource into its HTML counterpart
     * @param resource resource corresponding to en-media tag
     * @return Result with valid HTML representing the resource/en-media tag in
     *         case of success or error string in case of failure
     */
    [[nodiscard]] virtual Result<QString, ErrorString> convertResource(
        const qevercloud::Resource & resource) const = 0;
};

} // namespace quentier::enml
