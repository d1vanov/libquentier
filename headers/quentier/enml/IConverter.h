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
#include <quentier/enml/conversion_rules/Fwd.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/Result.h>
#include <quentier/utility/Linkage.h>

#include <QList>
#include <QStringList>
#include <QTextDocument>

#include <qevercloud/types/Note.h>

namespace quentier::enml {

/**
 * @brief The IConverter interface encapsulates a set of methods performing
 * conversions between ENML and other note content formats, namely HTML
 */
class QUENTIER_EXPORT IConverter
{
public:
    virtual ~IConverter();

    /**
     * Converts HTML representation of note content into ENML
     * @param html HTML representation of note content
     * @param decryptedTextCache cache of decrypted text fragments
     * @param skipRules skip rules to be used during the conversion
     * @return Result with ENML in case of success or error string in case of
     *         failure
     */
    [[nodiscard]] virtual Result<QString, ErrorString> convertHtmlToEnml(
        const QString & html, IDecryptedTextCache & decryptedTextCache,
        const QList<conversion_rules::ISkipRulePtr> & skipRules = {}) const = 0;

    /**
     * Convert HTML representation of note content into QTextDocument
     * @param html HTML representation of note content
     * @param doc QTextDocument into which the converted note content is put
     * @param skipRules skip rules to be used during the conversion
     * @return Valid result in case of success or error string in case of
     *         failure
     */
    [[nodiscard]] virtual Result<void, ErrorString> convertHtmlToDoc(
        const QString & html, QTextDocument & doc,
        const QList<conversion_rules::ISkipRulePtr> & skipRules = {}) const = 0;

    /**
     * Convert HTML representation of note content into a valid XML document
     * @param html HTML representation of note content
     * @return Result with XML in case of success of error string in case of
     * 		   failure
     */
    [[nodiscard]] virtual Result<QString, ErrorString> convertHtmlToXml(
        const QString & html) const = 0;

    /**
     * Convert HTML representation of note content into a valid XHTML document
     * @param html HTML representation of note content
     * @return Result with XHTML in case of success of error string in case of
     * 		   failure
     */
    [[nodiscard]] virtual Result<QString, ErrorString> convertHtmlToXhtml(
        const QString & html) const = 0;

    /**
     * Converts ENML into HTML representation of note content
     * @param enml ENML representation of note content
     * @param decryptedTextCache cache of decrypted text fragments
     * @return Result with HTML data in case of success or error string in case
     *         of failure
     */
    [[nodiscard]] virtual Result<IHtmlDataPtr, ErrorString> convertEnmlToHtml(
        const QString & enml,
        IDecryptedTextCache & decryptedTextCache) const = 0;

    /**
     * Converts ENML into plain text representation of note content
     * @param enml ENML representation of note content
     * @return Result with plain text representation of note content in case
     *         of success or error string in case of failure
     */
    [[nodiscard]] virtual Result<QString, ErrorString> convertEnmlToPlainText(
        const QString & enml) const = 0;

    /**
     * Converts ENML into a list of words
     * @param enml ENML representation of note content
     * @return Result with list of words in case of success or error string in
     *         case of failure
     */
    [[nodiscard]] virtual Result<QStringList, ErrorString>
        convertEnmlToWordsList(const QString & enml) const = 0;

    /**
     * Converts plain text into a list of words
     * @param plainText plain text representation of note content
     * @return list of words
     */
    [[nodiscard]] virtual QStringList convertPlainTextToWordsList(
        const QString & plainText) const = 0;

    /**
     * Validates ENML against rules
     * @param enml ENML representation of note content
     * @return valid Result in case of success or error string in case of
     *         failure
     */
    [[nodiscard]] virtual Result<void, ErrorString> validateEnml(
        const QString & enml) const = 0;

    /**
     * Validates ENML and attempts to fix it automatically if it's not valid
     * @param enml ENML representation of note content
     * @return Result with either unchanged or fixed up ENML in case of success
     *         or error string in case of failure
     */
    [[nodiscard]] virtual Result<QString, ErrorString> validateAndFixupEnml(
        const QString & enml) const = 0;

    /**
     * @brief The EnexExportTags enum allows to specify whether export of
     * note(s) to ENEX should include the names of note's tags or not.
     */
    enum class EnexExportTags
    {
        Yes = 0,
        No
    };

    /**
     * Exports a list of notes into ENEX
     * @param notes notes to be exported into ENEX
     * @param tagNamesByTagLocalIds mapper from tag local ids into tag names
     * @param exportTagsOption option controlling the export of tag names
     * @param version optional version tag for ENEX, omitted if not set
     * @return Result with ENEX in case of success or error string in case of
     *         failure
     */
    [[nodiscard]] virtual Result<QString, ErrorString> exportNotesToEnex(
        const QList<qevercloud::Note> & notes,
        const QHash<QString, QString> & tagNamesByTagLocalIds,
        EnexExportTags exportTagsOption,
        const QString & version = {}) const = 0;

    /**
     * Import notes from ENEX
     * @param enex ENEX to be used for import
     * @return Result with list of notes in case of success or error string in
     *         case of failure
     * @note if tag names are present in ENEX, corresponding notes would have
     *       their tagNames field filled
     */
    [[nodiscard]] virtual Result<QList<qevercloud::Note>, ErrorString>
        importEnex(const QString & enex) const = 0;
};

} // namespace quentier::enml
